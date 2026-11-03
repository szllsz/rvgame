`timescale 1ns / 1ps
// 参数化只读缓存，默认4 KiB：64行，每行64字节；单个在途AXI INCR请求。
// CPU保持request/address直到ready；ready周期的数据/error就是该请求结果。
// address为DDR缓冲区内偏移，ddr_base须64字节对齐且请求期间保持不变。
// 改写DDR内容或修改base之后必须flush，PS还须先完成CPU缓存flush。
// flush只清有效位，不取消已发出的AXI事务，填充完成也不会重新发布该行。
module ddr_read_cache #(
    parameter CACHE_LINES = 64, // 总行数，2的幂；默认4 KiB，256行为16 KiB
    parameter CACHE_WAYS = 1, // 1/2/4路；优先使用无效路，满组按轮转替换
    parameter EARLY_HIT = 0, // 1在同步BRAM输出有效的LOOKUP拍返回命中；默认保留两拍
    parameter RESPONSE_CHAIN = 0, // ready沿可接收独立next请求，当前响应仍属于原request
    parameter SCAN_NEXT_LINE = 0, // 行尾仅接续已有效的下一缓存行，绝不产生AXI请求
    parameter HIT_LINE_SCAN = 0, // 仅流送已验证缓存行，不产生AXI请求
    parameter DECOUPLED_READ_HINT = 0,
    parameter SEPARATE_SCAN_READ = 0, // Share scan reads with the otherwise idle fill port.
    parameter CHAIN_READ_AHEAD = 0 // 旧请求已锁存后提前读next，ready只控制交接沿，不进入BRAM地址选择
) (
    input wire clk,
    input wire reset_n,
    input wire flush,
    input wire [31:0] ddr_base,
    input wire request,
    input wire [31:0] address,
    input wire next_request,
    input wire next_read_hint,
    input wire [31:0] next_address,
    output wire ready,
    output wire [31:0] read_data,
    output wire error,
    output wire busy,
    output wire [31:0] m_axi_araddr,
    output wire [7:0] m_axi_arlen,
    output wire [2:0] m_axi_arsize,
    output wire [1:0] m_axi_arburst,
    output wire [3:0] m_axi_arcache,
    output wire [2:0] m_axi_arprot,
    output wire m_axi_arvalid,
    input wire m_axi_arready,
    input wire [31:0] m_axi_rdata,
    input wire [1:0] m_axi_rresp,
    input wire m_axi_rlast,
    input wire m_axi_rvalid,
    output wire m_axi_rready,
    // 只读填充旁路；commit沿与缓存有效位发布沿一致，失败/flush的整行不能发布。
    output wire refill_start,
    output wire refill_write,
    output wire [31:0] refill_address,
    output wire [31:0] refill_data,
    output wire refill_commit,
    input wire instruction_hit_request,
    input wire scan_continue,
    output wire scan_valid,
    output wire [31:0] scan_address,
    output wire [31:0] scan_data
);
    localparam IDLE = 3'd0;
    localparam LOOKUP = 3'd1;
    localparam ISSUE = 3'd2;
    localparam FILL = 3'd3;
    localparam DRAIN = 3'd4;
    localparam RESPONSE = 3'd5;
    localparam SETS = CACHE_LINES / CACHE_WAYS;
    localparam INDEX_BITS = $clog2(SETS);
    localparam WAY_BITS = CACHE_WAYS == 1 ? 1 : $clog2(CACHE_WAYS);
    reg [2:0] state;
    reg [31:0] saved_address, saved_base, burst_address;
    reg [31:0] response_data;
    reg response_error, fill_error, flush_pending;
    reg [3:0] beat;
    reg [CACHE_LINES-1:0] valid;
    reg [25-INDEX_BITS:0] tags [0:CACHE_LINES-1];
    wire [31:0] cache_words [0:CACHE_WAYS-1];
    reg [31:0] hit_data;
    wire [31:0] scan_words [0:CACHE_WAYS-1];
    reg [WAY_BITS-1:0] victims [0:SETS-1];
    reg [WAY_BITS-1:0] fill_way, hit_way, replacement_way;
    reg hit, invalid_found, fill_full;
    integer way, set_index;
    wire [INDEX_BITS-1:0] index = saved_address[INDEX_BITS+5:6];
    wire chain_accept = RESPONSE_CHAIN && next_request && ready && !error && !flush_pending && !flush;
    wire read_next = CHAIN_READ_AHEAD ? (RESPONSE_CHAIN && next_request && state != IDLE) : chain_accept;
    reg scan_active;
    reg [31:0] scan_word_address;
    reg [WAY_BITS-1:0] scan_way;
    reg [4:0] scan_left;
    wire early_hit;
    wire scan_start = HIT_LINE_SCAN && early_hit && instruction_hit_request && !chain_accept;
    wire [3:0] first_scan_word = saved_address[5:2] + 1'b1;
    wire [3:0] next_scan_word = scan_word_address[5:2] + 1'b1;
    wire [31:0] following_line = {scan_word_address[31:6],6'd0} + 32'd64;
    wire [INDEX_BITS-1:0] following_index = following_line[INDEX_BITS+5:6];
    reg following_hit;
    reg [WAY_BITS-1:0] following_way;
    integer following_bank;
    // 独立tag读地址来自已寄存的scan地址，保持CPU请求和响应接口不变。
    always @(*) begin
        following_hit = 1'b0;
        following_way = 0;
        for(following_bank=0; following_bank<CACHE_WAYS; following_bank=following_bank+1)
            if(!following_hit && valid[following_bank*SETS+following_index] &&
                tags[following_bank*SETS+following_index] == following_line[31:INDEX_BITS+6]) begin
                following_hit = 1'b1;
                following_way = following_bank;
            end
    end
    wire scan_cross = SCAN_NEXT_LINE && scan_valid && scan_continue && scan_left == 1 &&
        scan_word_address[5:2] == 15 && following_hit;
    // 正常请求/交接优先；scan_valid只对应上一沿已读入的BRAM字。
    wire [INDEX_BITS+3:0] read_index = read_next ? next_address[INDEX_BITS+5:2] :
        state == IDLE && request ? address[INDEX_BITS+5:2] :
        scan_start ? {saved_address[INDEX_BITS+5:6], first_scan_word} :
        scan_cross ? following_line[INDEX_BITS+5:2] :
        scan_valid ? {scan_word_address[INDEX_BITS+5:6], next_scan_word} :
        state == IDLE ? address[INDEX_BITS+5:2] : saved_address[INDEX_BITS+5:2];
    assign scan_valid = HIT_LINE_SCAN && scan_active && state == IDLE && reset_n && !flush && !flush_pending;
    assign scan_address = scan_word_address;
    // Speculation changes only an internal read port. Acceptance still uses
    // next_request, ready, error and flush, and scan owns a separate RAM port.
    wire normal_read_next = DECOUPLED_READ_HINT && SEPARATE_SCAN_READ && CHAIN_READ_AHEAD ?
        (RESPONSE_CHAIN && next_read_hint && state != IDLE) : read_next;
    wire [INDEX_BITS+3:0] normal_read_index = normal_read_next ? next_address[INDEX_BITS+5:2] :
        state == IDLE ? address[INDEX_BITS+5:2] : saved_address[INDEX_BITS+5:2];
    // Only the actual scan_start publishes this speculative internal RAM read.
    // LOOKUP already owns saved_address, so its first scan word can be read
    // without waiting for instruction request/chain-accept combinational logic.
    // The separate scan port can pre-read the next line before tag acceptance.
    // scan_cross still qualifies address/way/valid publication; no hit means
    // this internal RAM output is discarded, including during flush/cancel.
    wire [INDEX_BITS+3:0] scan_read_index = state == LOOKUP ? {saved_address[INDEX_BITS+5:6], first_scan_word} :
        SCAN_NEXT_LINE && scan_left == 1 && scan_word_address[5:2] == 15 ? following_line[INDEX_BITS+5:2] :
        {scan_word_address[INDEX_BITS+5:6], next_scan_word};
    assign scan_data = SEPARATE_SCAN_READ ? scan_words[scan_way] : cache_words[scan_way];
    always @(posedge clk) begin
        if (!reset_n || flush) begin
            scan_active <= 1'b0;
            scan_word_address <= 0;
            scan_way <= 0;
            scan_left <= 0;
        end else if (scan_start) begin
            scan_active <= 1'b1;
            scan_word_address <= {saved_address[31:6], first_scan_word, 2'b00};
            scan_way <= hit_way;
            scan_left <= 15;
        end else if (state != IDLE || request || chain_accept || !scan_continue) begin
            // 接收新读地址的沿清除scan；下拍BRAM输出归正常事务，不能再标记为scan。
            scan_active <= 1'b0;
        end else if (scan_cross) begin
            scan_word_address <= following_line;
            scan_way <= following_way;
            scan_left <= 16;
        end else if (scan_left == 1) begin
            scan_active <= 1'b0;
        end else if (scan_active) begin
            scan_word_address <= {scan_word_address[31:6], next_scan_word, 2'b00};
            scan_left <= scan_left - 1'b1;
        end
    end
    wire fill_write = state == FILL && m_axi_rvalid && m_axi_rready;
    assign refill_start = state == ISSUE && m_axi_arready;
    assign refill_write = fill_write;
    assign refill_address = {saved_address[31:6], beat, 2'b00};
    assign refill_data = m_axi_rdata;
    assign refill_commit = fill_write && m_axi_rlast && beat == 4'd15 &&
        !fill_error && m_axi_rresp == 2'b00 && !flush_pending && !flush;
    assign early_hit = EARLY_HIT && state == LOOKUP && hit && !flush_pending && !flush;
    assign ready = state == RESPONSE || early_hit;
    assign read_data = early_hit ? hit_data : response_data;
    assign error = early_hit ? 1'b0 : response_error;
    assign busy = state != IDLE;
    assign m_axi_araddr = burst_address;
    assign m_axi_arlen = 8'd15;
    assign m_axi_arsize = 3'd2;
    assign m_axi_arburst = 2'b01;
    assign m_axi_arcache = 4'b0011;
    assign m_axi_arprot = 3'b000;
    assign m_axi_arvalid = state == ISSUE;
    assign m_axi_rready = state == FILL || state == DRAIN;

    // Reset only metadata, never RAM data; synchronous read infers BRAM.
    genvar bank;
    generate for (bank=0; bank<CACHE_WAYS; bank=bank+1) begin: way_ram
        (* ram_style = "block" *) reg [31:0] words [0:SETS*16-1];
        reg [31:0] cache_word;
        assign cache_words[bank] = cache_word;
        if (SEPARATE_SCAN_READ) begin : g_separate_scan
            reg [31:0] scan_word;
            wire bank_fill = fill_write && fill_way == bank;
            // Port A writes refills or reads scan data. Port B reads normal requests.
            // Scan cannot publish during a fill; collisions are not observable responses.
            wire [INDEX_BITS+3:0] port_a_index = bank_fill ? {index, beat} : scan_read_index;
            always @(posedge clk) begin
                if (bank_fill) words[port_a_index] <= m_axi_rdata;
                scan_word <= words[port_a_index];
            end
            always @(posedge clk) cache_word <= words[normal_read_index];
            assign scan_words[bank] = scan_word;
        end else begin : g_shared_scan
            always @(posedge clk) begin
                cache_word <= words[read_index];
                if (fill_write && fill_way == bank) words[{index, beat}] <= m_axi_rdata;
            end
            assign scan_words[bank] = 32'd0;
        end
    end endgenerate

    // Free-running internal tag reads avoid the completion-to-enable path.
    // On every actual IDLE/chain acceptance edge the index matches its owner.
    // Only LOOKUP consumes this snapshot; later speculative values are ignored.
    // Publication, flush and chain acceptance remain controlled by the FSM.
    reg [25-INDEX_BITS:0] lookup_tags [0:CACHE_WAYS-1];
    reg [CACHE_WAYS-1:0] lookup_valid;
    wire [INDEX_BITS-1:0] lookup_index = state == IDLE ?
        address[INDEX_BITS+5:6] : next_address[INDEX_BITS+5:6];
    integer lookup_bank;
    always @(posedge clk) begin
        for (lookup_bank=0; lookup_bank<CACHE_WAYS; lookup_bank=lookup_bank+1) begin
            lookup_tags[lookup_bank] <= tags[lookup_bank*SETS+lookup_index];
            lookup_valid[lookup_bank] <= valid[lookup_bank*SETS+lookup_index];
        end
    end

    // Match each way in parallel and select payload without encoding a way
    // number then decoding it through a wide mux. Lowest-way priority is
    // preserved even for duplicate tags; the transaction FSM is unchanged.
    wire [CACHE_WAYS-1:0] data_way_match,data_way_select;
    genvar data_bank;
    generate for(data_bank=0;data_bank<CACHE_WAYS;data_bank=data_bank+1) begin:g_hit_data
        assign data_way_match[data_bank]=lookup_valid[data_bank] &&
            lookup_tags[data_bank]==saved_address[31:INDEX_BITS+6];
        if(data_bank==0) begin:g_first
            assign data_way_select[data_bank]=data_way_match[data_bank];
        end else begin:g_other
            assign data_way_select[data_bank]=data_way_match[data_bank] &&
                !(|data_way_match[data_bank-1:0]);
        end
    end endgenerate
    integer data_way;
    always @* begin
        hit_data=32'd0;
        for(data_way=0;data_way<CACHE_WAYS;data_way=data_way+1)
            hit_data=hit_data | (cache_words[data_way] & {32{data_way_select[data_way]}});
    end

    // Parallel bank reads retain the original two-cycle hit response.
    always @(*) begin
        hit = 1'b0;
        hit_way = 0;
        invalid_found = 1'b0;
        replacement_way = victims[index];
        for (way=0; way<CACHE_WAYS; way=way+1) begin
            if (!hit && lookup_valid[way] &&
                lookup_tags[way] == saved_address[31:INDEX_BITS+6]) begin
                hit = 1'b1;
                hit_way = way;
            end
            if (!invalid_found && !valid[way*SETS+index]) begin
                invalid_found = 1'b1;
                replacement_way = way;
            end
        end
    end

    always @(posedge clk) begin
        if (!reset_n) begin
            state <= IDLE;
            valid <= {CACHE_LINES{1'b0}};
            saved_address <= 32'd0;
            saved_base <= 32'd0;
            burst_address <= 32'd0;
            response_data <= 32'd0;
            response_error <= 1'b0;
            fill_error <= 1'b0;
            flush_pending <= 1'b0;
            beat <= 4'd0;
            fill_way <= 0;
            fill_full <= 1'b0;
            for (set_index=0; set_index<SETS; set_index=set_index+1)
                victims[set_index] <= 0;
        end else begin
            case (state)
                IDLE: begin
                    flush_pending <= 1'b0;
                    if (request) begin
                        saved_address <= address;
                        saved_base <= ddr_base;
                        response_error <= 1'b0;
                        if (address[1:0] != 2'b00 || ddr_base[5:0] != 6'd0) begin
                            response_error <= 1'b1;
                            response_data <= 32'd0;
                            state <= RESPONSE;
                        end else state <= LOOKUP;
                    end
                end
                LOOKUP: begin
                    if (hit && !flush_pending && !flush) begin
                        response_data <= hit_data;
                        state <= EARLY_HIT ? IDLE : RESPONSE;
                    end else begin
                        fill_way <= replacement_way;
                        fill_full <= !invalid_found;
                        valid[replacement_way*SETS+index] <= 1'b0;
                        burst_address <= saved_base + {saved_address[31:6], 6'd0};
                        beat <= 4'd0;
                        fill_error <= 1'b0;
                        state <= ISSUE;
                    end
                end
                ISSUE: if (m_axi_arready) state <= FILL;
                FILL: begin
                    if (m_axi_rvalid) begin
                        if (beat == saved_address[5:2]) response_data <= m_axi_rdata;
                        if (m_axi_rresp != 2'b00) fill_error <= 1'b1;
                        if (m_axi_rlast) begin
                            response_error <= fill_error || m_axi_rresp != 2'b00 || beat != 4'd15;
                            if (!fill_error && m_axi_rresp == 2'b00 && beat == 4'd15 &&
                                !flush_pending && !flush) begin
                                tags[fill_way*SETS+index] <= saved_address[31:INDEX_BITS+6];
                                valid[fill_way*SETS+index] <= 1'b1;
                                if (fill_full)
                                    victims[index] <= fill_way == CACHE_WAYS-1 ? 0 : fill_way + 1'b1;
                            end
                            state <= RESPONSE;
                        end else if (beat == 4'd15) begin
                            // Missing RLAST: consume excess beats without corrupting RAM.
                            response_error <= 1'b1;
                            state <= DRAIN;
                        end else beat <= beat + 1'b1;
                    end
                end
                DRAIN: if (m_axi_rvalid && m_axi_rlast) state <= RESPONSE;
                RESPONSE: state <= IDLE;
                default: state <= IDLE;
            endcase
            // 在旧响应被消费的沿切换地址；同步BRAM下一拍对应新请求，保留单个AXI事务。
            if (chain_accept) begin
                saved_address <= next_address;
                saved_base <= ddr_base;
                response_error <= 1'b0;
                if (next_address[1:0] != 2'b00 || ddr_base[5:0] != 6'd0) begin
                    response_error <= 1'b1;
                    response_data <= 32'd0;
                    state <= RESPONSE;
                end else state <= LOOKUP;
            end
            if (flush) begin
                valid <= {CACHE_LINES{1'b0}};
                flush_pending <= 1'b1;
                for (set_index=0; set_index<SETS; set_index=set_index+1)
                    victims[set_index] <= 0;
            end
        end
    end
endmodule
