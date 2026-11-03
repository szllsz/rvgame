`timescale 1ns/1ps
// Stage G main2 unit prototype: four async reads and two distinct-register writes.
// Last-write-port selection follows the established multi-copy RF mechanism:
// biRISC-V 6af9c4be5a0807d368eaad5e49af52322e31d073 (Apache-2.0).
// Local contract adds explicit enables, 32-cycle initialization/ready isolation,
// synchronous/asynchronous control reset and whole-pair rejection on same-rd WAW.
module rv32i_banked_regfile #(parameter SYNC_RESET=1) (
    input wire clk,input wire reset_n,output wire ready,output wire write_pair_ready,
    input wire [4:0] read_addr0,read_addr1,read_addr2,read_addr3,
    output wire [31:0] read_data0,read_data1,read_data2,read_data3,
    input wire write_enable0,write_enable1,
    input wire [4:0] write_addr0,write_addr1,
    input wire [31:0] write_data0,write_data1
);
    (* ram_style = "distributed" *) reg [31:0] write0_words [0:31];
    (* ram_style = "distributed" *) reg [31:0] write1_words [0:31];
    reg [31:0] last_writer;
    reg [4:0] clear_index;
    reg initialized;
    wire write0_live=write_enable0 && write_addr0!=0;
    wire write1_live=write_enable1 && write_addr1!=0;
    wire write_conflict=write0_live && write1_live && write_addr0==write_addr1;
    assign ready=reset_n && initialized;
    assign write_pair_ready=ready && !write_conflict;
    assign read_data0=ready && read_addr0!=0 ? (last_writer[read_addr0] ? write1_words[read_addr0] : write0_words[read_addr0]) : 32'd0;
    assign read_data1=ready && read_addr1!=0 ? (last_writer[read_addr1] ? write1_words[read_addr1] : write0_words[read_addr1]) : 32'd0;
    assign read_data2=ready && read_addr2!=0 ? (last_writer[read_addr2] ? write1_words[read_addr2] : write0_words[read_addr2]) : 32'd0;
    assign read_data3=ready && read_addr3!=0 ? (last_writer[read_addr3] ? write1_words[read_addr3] : write0_words[read_addr3]) : 32'd0;
    // Only the selected initial copy needs clearing. Resetting last_writer hides
    // every stale write1 payload until a newly accepted write1 owns that register.
    always @(posedge clk) begin
        if(reset_n) begin
            if(!initialized) write0_words[clear_index]<=0;
            else if(write_pair_ready && write0_live) write0_words[write_addr0]<=write_data0;
        end
        if(reset_n && write_pair_ready && write1_live) write1_words[write_addr1]<=write_data1;
    end
    task step_control;
    begin
        if(!reset_n) begin clear_index<=0;initialized<=0;last_writer<=0;end
        else if(!initialized) begin
            if(clear_index==31) initialized<=1;
            else clear_index<=clear_index+1'b1;
        end else if(write_pair_ready) begin
            if(write0_live) last_writer[write_addr0]<=0;
            if(write1_live) last_writer[write_addr1]<=1;
        end
    end
    endtask
    generate if(SYNC_RESET) begin:g_sync_initialize
        always @(posedge clk) step_control();
    end else begin:g_async_initialize
        always @(posedge clk or negedge reset_n) step_control();
    end endgenerate
endmodule
