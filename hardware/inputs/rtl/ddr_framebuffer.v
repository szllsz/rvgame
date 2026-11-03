`timescale 1ns / 1ps
// 640x480 RGB332: BRAM staging -> AXI3 HP1 DDR double buffers -> two line caches.
// Each write is a 16-beat/64-byte INCR burst; scan reads split at 4 KiB boundaries.
// A completed B response is required before publishing a new display frame.
module ddr_framebuffer #(parameter NATIVE800=0) (
    input wire clk,
    input wire reset_n,
    input wire enable,
    input wire [31:0] base,
    input wire front,
    input wire display_valid,
    input wire copy_start,
    input wire copy_band,copy_bank,copy_cancel,
    output reg copy_done,
    output wire copy_busy,
    output wire [18:0] source_address,
    input wire [31:0] source_data,
    input wire scan_de,
    input wire [10:0] scan_x,
    input wire [9:0] scan_y,
    output reg [23:0] scan_rgb,
    output wire bus_busy,
    output reg fault,
    output reg [31:0] underflows,
    output reg [31:0] write_frames,
    output wire [31:0] araddr,
    output wire [3:0] arlen,
    output wire arvalid,
    input wire arready,
    input wire [31:0] rdata,
    input wire [1:0] rresp,
    input wire rlast,
    input wire rvalid,
    output wire rready,
    output wire [31:0] awaddr,
    output wire [3:0] awlen,
    output wire awvalid,
    input wire awready,
    output wire [31:0] wdata,
    output wire [3:0] wstrb,
    output wire wlast,
    output wire wvalid,
    input wire wready,
    input wire [1:0] bresp,
    input wire bvalid,
    output wire bready
);
    localparam WR_IDLE=0, WR_GATHER=1, WR_AW=2, WR_DATA=3, WR_RESP=4;
    reg [2:0] wr_state;
    reg [18:0] copy_offset;
    reg target_bank,target_band,cancelled;
    reg [31:0] target_base;
    localparam COPY_BYTES=NATIVE800 ? 240000 : 307200;
    localparam LINE_WORDS=NATIVE800 ? 200 : 160;
    reg [4:0] issued, captured;
    reg source_pending;
    reg [3:0] wr_beat;
    // Keep the current burst immutable until B; collect the next burst in the other half.
    (* ram_style="distributed" *) reg [31:0] burst_data [0:31];
    reg read_buffer,fill_buffer;
    reg [1:0] buffer_valid;
    reg [18:0] gather_offset;
    wire gather_issue = wr_state!=WR_IDLE && !buffer_valid[fill_buffer] &&
        gather_offset<COPY_BYTES && issued<16;
    wire gather_capture = wr_state!=WR_IDLE && source_pending;
    wire gather_publish = gather_capture && captured==15;
    wire consume_buffer = wr_state==WR_RESP && bvalid;
    assign source_address = gather_offset + {12'd0,issued,2'b00};
    // No reset on the data array: one synchronous write port, asynchronous read.
    always @(posedge clk) begin
        if (gather_capture) burst_data[{fill_buffer,captured[3:0]}]<=source_data;
    end
    assign copy_busy = wr_state!=WR_IDLE;
    assign awaddr = (NATIVE800 ? target_base : base) + (target_bank ? 32'h80000 : 32'd0) +
        (NATIVE800 && target_band ? 32'd240000 : 0) + {13'd0,copy_offset};
    assign awlen = 4'd15;
    assign awvalid = wr_state==WR_AW;
    assign wdata = burst_data[{read_buffer,wr_beat}];
    assign wstrb = 4'b1111;
    assign wlast = wr_beat==15;
    assign wvalid = wr_state==WR_DATA;
    assign bready = wr_state==WR_RESP;

    localparam RD_IDLE=0, RD_ADDR=1, RD_DATA=2;
    reg [1:0] rd_state;
    reg [9:0] rd_line;
    reg rd_bank, rd_bad;
    reg [7:0] rd_word;
    reg [4:0] rd_count, rd_beat;
    // Two 640-byte lines, asynchronous read and synchronous DDR refill.
    (* ram_style="distributed" *) reg [31:0] line0 [0:LINE_WORDS-1];
    (* ram_style="distributed" *) reg [31:0] line1 [0:LINE_WORDS-1];
    reg [9:0] tag0,tag1;
    reg valid0,valid1,previous_front,previous_enable;
    // Nearest neighbour 5:4 in both axes. Phase zero repeats the source
    // sample: 0,0,1,2,3,4,4,... No pixel-rate divider or larger framebuffer.
    wire inside = scan_x>=112 && scan_x<912 && scan_y<600;
    reg [9:0] view_x;
    reg [8:0] line;
    reg [2:0] phase_x,phase_y;
    reg previous_de;
    wire [9:0] active_line=NATIVE800 ? scan_y : {1'b0,line};
    wire [9:0] active_x=NATIVE800 ? scan_x-11'd112 : view_x;
    wire hit = active_line[0] ? valid1 && tag1==active_line : valid0 && tag0==active_line;
    // During repeated output rows keep the CURRENT source bank intact.
    // Prime row zero in vertical blanking, including after front changes.
    wire [9:0] wanted = scan_y>=600 ? 10'd0 : !hit ? active_line :
        active_line<(NATIVE800 ? 599 : 479) ? active_line+10'd1 : 10'd0;
    wire wanted_hit = wanted[0] ? valid1 && tag1==wanted : valid0 && tag0==wanted;
    wire [31:0] scan_word = active_line[0] ? line1[active_x[9:2]] : line0[active_x[9:2]];
    wire [7:0] pixel = scan_word >> {active_x[1:0],3'b000};
    wire [31:0] read_address = base + (rd_bank ? 32'h80000 : 32'd0) +
        (NATIVE800 ? (({22'd0,rd_line}<<9)+({22'd0,rd_line}<<8)+({22'd0,rd_line}<<5)) : (({22'd0,rd_line}<<9)+({22'd0,rd_line}<<7))) + {22'd0,rd_word,2'b00};
    wire [10:0] page_words = (13'd4096-{1'b0,read_address[11:0]})>>2;
    wire [8:0] line_words = LINE_WORDS-{1'b0,rd_word};
    wire [4:0] burst_words = line_words<16 ? line_words[4:0] : 5'd16;
    wire [4:0] next_count = page_words<burst_words ? page_words[4:0] : burst_words;
    assign araddr = read_address;
    assign arlen = rd_count[3:0]-4'd1;
    assign arvalid = rd_state==RD_ADDR && rd_count!=0;
    assign rready = rd_state==RD_DATA;
    assign bus_busy = copy_busy || rd_state!=RD_IDLE;
    always @(posedge clk) begin
        if (!reset_n) begin
            wr_state<=WR_IDLE;copy_offset<=0;target_bank<=0;target_band<=0;target_base<=0;cancelled<=0;
            issued<=0;captured<=0;source_pending<=0;wr_beat<=0;copy_done<=0;
            read_buffer<=0;fill_buffer<=0;buffer_valid<=0;gather_offset<=0;
            rd_state<=RD_IDLE;rd_line<=0;rd_bank<=0;rd_word<=0;
            rd_count<=0;rd_beat<=0;rd_bad<=0;valid0<=0;valid1<=0;
            tag0<=0;tag1<=0;previous_front<=0;previous_enable<=0;
            fault<=0;underflows<=0;write_frames<=0;scan_rgb<=0;
            view_x<=0;line<=0;phase_x<=0;phase_y<=0;previous_de<=0;
        end else begin
            previous_de<=scan_de;
            if (!scan_de || scan_x<112) begin view_x<=0;phase_x<=0;end
            else if (inside) begin
                phase_x<=phase_x==4 ? 0 : phase_x+1'b1;
                if (phase_x!=0) view_x<=view_x+1'b1;
            end
            if (scan_y>=600) begin line<=0;phase_y<=0;end
            else if (previous_de && !scan_de) begin
                phase_y<=phase_y==4 ? 0 : phase_y+1'b1;
                if (phase_y!=0 && line<479) line<=line+1'b1;
            end
            copy_done<=0;previous_front<=front;previous_enable<=enable;
            if (front!=previous_front || !enable) begin valid0<=0;valid1<=0;end
            if (enable && !previous_enable) begin fault<=0;underflows<=0;write_frames<=0;end
            if (scan_de && inside && enable && display_valid && hit)
                scan_rgb<={pixel[7:5],pixel[7:5],pixel[7:6],pixel[4:2],pixel[4:2],pixel[4:3],
                           pixel[1:0],pixel[1:0],pixel[1:0],pixel[1:0]};
            else scan_rgb<=0;
            if (scan_de && scan_x==112 && inside && enable && display_valid && !hit)
                underflows<=underflows+1'b1;
            // An interrupted accepted transaction still drains, but must never
            // publish a tag if enable/front changes and changes back mid-burst.
            if (rd_state!=RD_IDLE && (!enable || front!=previous_front)) rd_bad<=1;
            if (NATIVE800 && wr_state!=WR_IDLE && (!enable || copy_cancel || fault)) cancelled<=1;
            if (wr_state!=WR_IDLE) begin
                source_pending<=gather_issue;
                if (gather_issue) issued<=issued+1'b1;
                if (gather_capture) captured<=captured+1'b1;
                if (gather_publish) begin
                    fill_buffer<=!fill_buffer;gather_offset<=gather_offset+19'd64;
                    issued<=0;captured<=0;
                end
                buffer_valid<=(buffer_valid | (gather_publish ? (2'b01<<fill_buffer) : 2'b00)) &
                    ~(consume_buffer ? (2'b01<<read_buffer) : 2'b00);
            end else source_pending<=0;
            case (wr_state)
                WR_IDLE: if (copy_start && enable && !fault) begin
                    target_bank<=NATIVE800 ? copy_bank : !front;target_band<=copy_band;target_base<=base;cancelled<=0;copy_offset<=0;issued<=0;captured<=0;
                    source_pending<=0;wr_state<=WR_GATHER;
                    read_buffer<=0;fill_buffer<=0;buffer_valid<=0;gather_offset<=0;
                end
                WR_GATHER: if (NATIVE800 && (cancelled || copy_cancel || !enable)) wr_state<=WR_IDLE;
                else if (buffer_valid[read_buffer]) wr_state<=WR_AW;
                WR_AW: if (awready) begin wr_beat<=0;wr_state<=WR_DATA;end
                WR_DATA: if (wready) begin
                    if (wr_beat==15) wr_state<=WR_RESP;
                    else wr_beat<=wr_beat+1'b1;
                end
                WR_RESP: if (bvalid) begin
                    if (bresp!=0) begin fault<=1;wr_state<=WR_IDLE;end
                    else if (!enable || (NATIVE800 && (cancelled || copy_cancel || fault))) wr_state<=WR_IDLE;
                    else if (copy_offset==COPY_BYTES-64) begin
                        copy_done<=1;write_frames<=write_frames+1'b1;wr_state<=WR_IDLE;
                    end else begin
                        copy_offset<=copy_offset+19'd64;read_buffer<=!read_buffer;
                        wr_state<=buffer_valid[!read_buffer] ? WR_AW : WR_GATHER;
                    end
                end
                default: wr_state<=WR_IDLE;
            endcase
            case (rd_state)
                RD_IDLE: if (enable && display_valid && !fault && !wanted_hit && front==previous_front) begin
                    rd_line<=wanted;rd_bank<=front;rd_word<=0;rd_beat<=0;rd_bad<=0;
                    // First address is calculated next cycle, before ARVALID.
                    rd_count<=0;rd_state<=RD_ADDR;
                end
                RD_ADDR: begin
                    if (rd_count==0) rd_count<=next_count;
                    else if (arready) begin rd_beat<=0;rd_state<=RD_DATA;end
                end
                RD_DATA: if (rvalid) begin
                    if (rresp!=0 || rlast!=(rd_beat==rd_count-1)) begin fault<=1;rd_bad<=1;end
                    if (rd_line[0]) line1[rd_word+rd_beat]<=rdata;
                    else line0[rd_word+rd_beat]<=rdata;
                    if (rlast) begin
                        if (!enable || rd_bank!=front || rd_bad || rresp!=0 || rd_beat!=rd_count-1)
                            rd_state<=RD_IDLE;
                        else if ({1'b0,rd_word}+rd_count==LINE_WORDS) begin
                            if (rd_line[0]) begin tag1<=rd_line;valid1<=1;end
                            else begin tag0<=rd_line;valid0<=1;end
                            rd_state<=RD_IDLE;
                        end else begin rd_word<=rd_word+rd_count;rd_count<=0;rd_state<=RD_ADDR;end
                    end else rd_beat<=rd_beat+1'b1;
                end
                default: rd_state<=RD_IDLE;
            endcase
        end
    end
endmodule
