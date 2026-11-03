`timescale 1ns/1ps
// CPU holds an unqualified read/write intent until the reply is ready.
// write_mask is the original byte mask, not a strobe gated by write_ready.
// quiescent describes this bridge only; cache/RAM draining is a separate owner.
module cpu_peripheral_cdc #(parameter SHARED_VIDEO_RESET=0,parameter POSTED_WRITES=0) (
    input wire cpu_clk,input wire peripheral_clk,input wire reset_n,input wire cpu_hold,
    input wire peripheral_reset_n,
    input wire read_request,input wire write_request,
    input wire [31:0] address,input wire [31:0] write_data,input wire [3:0] write_mask,
    output wire read_ready,output wire write_ready,output wire [31:0] read_data,
    output wire quiescent,
    output wire [31:0] peripheral_address,output wire [31:0] peripheral_write_data,
    output wire [3:0] peripheral_write_mask,output wire peripheral_write_enable,
    input wire peripheral_write_ready,input wire [31:0] peripheral_read_data
);
    wire request_valid,request_ready,response_valid,response_ready,peripheral_valid,peripheral_ready,cpu_reset_ready;
    wire [68:0] request_payload;
    wire peripheral_write;
    reg owned_write;
    wire mailbox_idle,fifo_source_idle,fifo_source_ready,fifo_dest_valid;
    wire [67:0] fifo_dest_write;
    generate if(POSTED_WRITES) begin:g_posted
        cpu_mmio_write_fifo u_writes(
            .src_clk(cpu_clk),.dst_clk(peripheral_clk),
            .src_reset_n(cpu_reset_ready),.dst_reset_n(peripheral_reset_n),
            .src_quiesce(cpu_hold),.src_valid(write_request && request_ready),
            .src_ready(fifo_source_ready),.src_write({write_mask,address,write_data}),
            .src_idle(fifo_source_idle),.dst_valid(fifo_dest_valid),
            .dst_ready(peripheral_write_ready),.dst_write(fifo_dest_write));
    end else begin:g_unposted
        assign fifo_source_idle=1'b1;assign fifo_source_ready=1'b0;
        assign fifo_dest_valid=1'b0;assign fifo_dest_write=68'd0;
    end endgenerate
    // Keep asynchronous mailbox reset/status out of CPU/cache admission logic.
    // Current idle alone is insufficient when a new owner is accepted at this edge.
    reg quiescent_reg;
    assign quiescent=quiescent_reg;
    always @(posedge cpu_clk) begin
        if(!cpu_reset_ready) quiescent_reg<=0;
        else quiescent_reg<=mailbox_idle && fifo_source_idle &&
            !(request_valid && request_ready) && !(POSTED_WRITES && write_request && write_ready);
    end
`ifndef SYNTHESIS
    initial if(POSTED_WRITES && !SHARED_VIDEO_RESET) $fatal(1,"POSTED_SHARED_RESET_REQUIRED");
    always @(posedge peripheral_clk) if(peripheral_reset_n && fifo_dest_valid && peripheral_valid)
        $fatal(1,"POSTED_READ_WRITE_OVERLAP");
`endif
    assign request_valid=!cpu_hold && !response_valid && (POSTED_WRITES ? (read_request && !write_request && fifo_source_idle) : (write_request || read_request));
    assign write_ready=POSTED_WRITES ? (fifo_source_ready && request_ready && !cpu_hold) : (response_valid && owned_write && !cpu_hold);
    assign read_ready=response_valid && !owned_write && !cpu_hold;
    assign response_ready=cpu_hold || (owned_write ? write_request : read_request);
    assign {peripheral_write,peripheral_write_mask,peripheral_address,peripheral_write_data}=fifo_dest_valid ? {1'b1,fifo_dest_write} : request_payload;
    assign peripheral_ready=!peripheral_write || peripheral_write_ready;
    assign peripheral_write_enable=(peripheral_valid || fifo_dest_valid) && peripheral_ready && peripheral_write;
    always @(posedge cpu_clk or negedge cpu_reset_ready) begin
        if(!cpu_reset_ready) owned_write<=0;
        else if(request_valid && request_ready) owned_write<=write_request;
    end
    cdc_request_reply #(.EXTERNAL_DST_RESET(SHARED_VIDEO_RESET)) u_mailbox(.dst_domain_reset_n(peripheral_reset_n),.src_clk(cpu_clk),.dst_clk(peripheral_clk),.reset_n(reset_n),
        .src_quiesce(cpu_hold),.src_idle(mailbox_idle),.src_reset_ready(cpu_reset_ready),.src_valid(request_valid),.src_ready(request_ready),
        .src_request({write_request,write_mask,address,write_data}),
        .src_response_valid(response_valid),.src_response_ready(response_ready),.src_response(read_data),
        .dst_valid(peripheral_valid),.dst_ready(peripheral_ready && !fifo_dest_valid),.dst_request(request_payload),
        .dst_response(peripheral_read_data));
endmodule
