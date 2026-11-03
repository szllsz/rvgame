`timescale 1ns/1ps
// Same-clock BRAM queue. PS writes exactly two little-endian PCM16 samples.
// The existing request/reply CDC owns the host transaction before this port.
// CPU controls run/pause; stop/hold flushes. Muting does not stop the clock.
module scarlet_pcm_stream #(
    parameter CLOCK_HZ=105000000,parameter SAMPLE_HZ=32000,
    parameter FIFO_WORDS=2048,parameter PREFILL_WORDS=64
)(
    input wire clk,input wire reset_n,input wire cpu_hold,
    input wire control_write,input wire [1:0] control_data,
    input wire host_write,input wire [31:0] host_data,
    input wire [7:0] cpu_address,input wire [7:0] host_address,
    output wire host_ready,output reg [31:0] cpu_read_data,host_read_data,
    output wire signed [15:0] sample
);
    localparam PTR_BITS=$clog2(FIFO_WORDS);
    (* ram_style="distributed" *) reg [31:0] memory[0:FIFO_WORDS-1];
    reg [PTR_BITS-1:0] write_pointer,read_pointer;
    reg [PTR_BITS:0] count;
    reg running,paused,started;
    reg [31:0] phase;
    reg [31:0] read_word,current_word;
    reg read_pending,word_valid,high_half;
    reg memory_write,memory_read,memory_return;
    reg [PTR_BITS-1:0] memory_write_address,memory_read_address;
    reg [31:0] memory_write_data;
    reg signed [15:0] held_sample;
    reg [31:0] consumed,underflows,ticks;
    reg [31:0] crc;
    reg [15:0] crc_word;
    reg [4:0] crc_bits;
    wire stopping=control_write && !control_data[0];
    wire active=running && !paused && started;
    wire tick=active && phase>=CLOCK_HZ-SAMPLE_HZ;
    wire accept_sample=tick && word_valid;
    wire pop_word=accept_sample && high_half;
    wire fetch=reset_n && !cpu_hold && !stopping && running && count!=0 && !word_valid && !read_pending;
    wire [15:0] next_sample=high_half ? current_word[31:16] : current_word[15:0];
    // A host request racing with CPU stop is acknowledged but discarded.
    // This prevents a normal exit from causing a PS bus error. A sender reads
    // RUN before queuing; stopped writes never reach the next application's FIFO.
    assign host_ready=!running || count<FIFO_WORDS;
    wire push=reset_n && host_write && host_ready && running && !stopping && !cpu_hold;
    assign sample=running && !paused && !cpu_hold ? held_sample : 16'sd0;
    wire [31:0] status={11'd0,(crc_bits!=0),word_valid,started,paused,running,4'd0,
                        {{(12-(PTR_BITS+1)){1'b0}},count}};
    always @* begin
        cpu_read_data=0;host_read_data=0;
        case(cpu_address)
            8'h94:cpu_read_data={30'd0,paused,running};
            8'h98:cpu_read_data=status;
            8'h9c:cpu_read_data=consumed;
            8'ha0:cpu_read_data=underflows;
            8'ha4:cpu_read_data=32'h50434d31;
            8'ha8:cpu_read_data=~crc;
            8'hac:cpu_read_data=ticks;
            8'hb0:cpu_read_data=SAMPLE_HZ;
            default:begin end
        endcase
        case(host_address)
            8'hb4:host_read_data=status;
            8'hb8:host_read_data=consumed;
            8'hbc:host_read_data=underflows;
            8'hc0:host_read_data=32'h50434d31;
            8'hc4:host_read_data=~crc;
            8'hc8:host_read_data=ticks;
            8'hcc:host_read_data=SAMPLE_HZ;
            default:begin end
        endcase
    end
    // Every BRAM control/address crosses a synchronous local register first.
    // The incoming CDC payload has asynchronous-reset flops; using its decode
    // directly as BRAM enable causes REQP-1839 and untimed reset assertion.
    // Count reserves a push immediately. The registered read is issued only
    // after that reservation, so a first-word write commits before its read.
    always @(posedge clk) begin
        if(!reset_n || cpu_hold || stopping) begin
            memory_write<=0;memory_read<=0;memory_return<=0;
        end else begin
            memory_write<=push;memory_read<=fetch;memory_return<=memory_read;
        end
        if(push) begin memory_write_address<=write_pointer;memory_write_data<=host_data;end
        if(fetch) memory_read_address<=read_pointer;
    end
    // Memory itself is never reset. Stop/hold invalidates all pending ownership;
    // a preceding committed write may retire but cannot leak into a new stream.
    always @(posedge clk) begin
        if(memory_write) memory[memory_write_address]<=memory_write_data;
        if(memory_read) read_word<=memory[memory_read_address];
        // Valid bits own this data; do not reset it. Vivado can absorb this
        // stage into BRAM's output register, whose reset must not receive the
        // upstream asynchronous-reset command decode (REQP-1839/RSTREGB).
        if(memory_return) current_word<=read_word;
    end
    always @(posedge clk) begin
        if(!reset_n || cpu_hold || stopping) begin
            write_pointer<=0;read_pointer<=0;count<=0;
            running<=0;paused<=0;started<=0;phase<=0;
            read_pending<=0;word_valid<=0;high_half<=0;held_sample<=0;
            consumed<=0;underflows<=0;ticks<=0;crc<=32'hffffffff;crc_word<=0;crc_bits<=0;
        end else begin
            if(control_write) begin running<=control_data[0];paused<=control_data[1];end
            if(running && !started && count>=PREFILL_WORDS && word_valid) started<=1;
            if(active) phase<=tick ? phase+SAMPLE_HZ-CLOCK_HZ : phase+SAMPLE_HZ;
            case({push,pop_word})
                2'b10:count<=count+1'b1;
                2'b01:count<=count-1'b1;
                default:begin end
            endcase
            if(push) write_pointer<=write_pointer+1'b1;
            if(fetch) read_pending<=1;
            else if(memory_return) begin read_pending<=0;word_valid<=1;high_half<=0;end
            if(tick) begin
                ticks<=ticks+1'b1;
                if(word_valid) begin
                    held_sample<=next_sample;consumed<=consumed+1'b1;
                    crc_word<=next_sample;crc_bits<=16;
                    if(high_half) begin word_valid<=0;high_half<=0;read_pointer<=read_pointer+1'b1;end
                    else high_half<=1;
                end else begin held_sample<=0;underflows<=underflows+1'b1;end
            end
            if(crc_bits!=0) begin
                crc<=((crc[0]^crc_word[0]) ? (crc>>1)^32'hedb88320 : crc>>1);
                crc_word<=crc_word>>1;crc_bits<=crc_bits-1'b1;
            end
        end
    end
`ifndef SYNTHESIS
    initial begin
        if(FIFO_WORDS<4 || (FIFO_WORDS&(FIFO_WORDS-1))!=0 || PTR_BITS+1>12 ||
           PREFILL_WORDS<1 || PREFILL_WORDS>FIFO_WORDS || CLOCK_HZ/SAMPLE_HZ<=20)
            $fatal(1,"Unsupported PCM FIFO/rate parameters");
    end
    always @(posedge clk) if(reset_n && !cpu_hold && !stopping) begin
        if(count>FIFO_WORDS) $fatal(1,"PCM FIFO count out of bounds");
        if(accept_sample && crc_bits!=0) $fatal(1,"PCM CRC did not finish before next sample");
        if(pop_word && count==0) $fatal(1,"PCM word ownership underflow");
    end
`endif
endmodule
