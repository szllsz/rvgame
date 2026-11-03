`timescale 1ns/1ps
// Four-entry posted-write transport with an owned video-domain head register.
// Caller must drain before reads/FENCE/HOLD reset and reset BOTH domains together.
// Gray-counter organization reviewed against pulp-platform/common_cells
// cc_cdc_fifo_gray; original project RTL, no upstream code copied.
module cpu_mmio_write_fifo (
    input wire src_clk,input wire dst_clk,
    input wire src_reset_n,input wire dst_reset_n,
    input wire src_quiesce,input wire src_valid,output wire src_ready,
    input wire [67:0] src_write,
    output wire src_idle,
    output wire dst_valid,input wire dst_ready,output wire [67:0] dst_write
);
    // Payload: byte mask, address, data. Registers preserve the full BRAM budget.
    (* ram_style="registers" *) reg [67:0] entries [0:3];
    reg [2:0] write_binary,write_gray,read_binary,read_gray;
    reg [67:0] head_payload;
    reg head_valid;
    (* ASYNC_REG="TRUE" *) reg [2:0] read_gray_meta,read_gray_sync;
    (* ASYNC_REG="TRUE" *) reg [2:0] write_gray_meta,write_gray_sync;
    reg source_full,source_idle;
    wire full=source_full;
    wire empty=read_gray==write_gray_sync;
    wire [2:0] write_next=write_binary+3'd1;
    wire [2:0] read_next=read_binary+3'd1;
    assign src_ready=src_reset_n && !src_quiesce && !full;
    assign src_idle=src_reset_n && source_idle;
    wire [2:0] accepted_write_binary=(src_valid && src_ready) ? write_next : write_binary;
    wire [2:0] accepted_write_gray=(accepted_write_binary>>1)^accepted_write_binary;
    // Return only synchronous functional status to CPU/cache ownership logic.
    // Gray pointers still clear asynchronously when either peer clock is stopped.
    // Computing from the accepted NEXT pointer prevents premature idle/full flags.
    always @(posedge src_clk) begin
        if(!src_reset_n) begin source_full<=1;source_idle<=0;end
        else begin
            source_full<=accepted_write_gray=={~read_gray_sync[2:1],read_gray_sync[0]};
            source_idle<=accepted_write_gray==read_gray_sync;
        end
    end
    assign dst_valid=dst_reset_n && head_valid;
    assign dst_write=head_payload;
    // No asynchronous reset on payload storage. Valid pointers own initialized words.
    always @(posedge src_clk)
        if(src_valid && src_ready) entries[write_binary[1:0]]<=src_write;
    always @(posedge src_clk or negedge src_reset_n) begin
        if(!src_reset_n) begin
            write_binary<=0;write_gray<=0;read_gray_meta<=0;read_gray_sync<=0;
        end else begin
            read_gray_meta<=read_gray;read_gray_sync<=read_gray_meta;
            if(src_valid && src_ready) begin
                write_binary<=write_next;write_gray<=(write_next>>1)^write_next;
            end
        end
    end
    // Capture the owned word before any peripheral decode. Its FIFO slot
    // remains allocated until the peripheral commits, including under HOLD.
    always @(posedge dst_clk)
        if(dst_reset_n && !head_valid && !empty) head_payload<=entries[read_binary[1:0]];
    always @(posedge dst_clk or negedge dst_reset_n) begin
        if(!dst_reset_n) begin
            read_binary<=0;read_gray<=0;write_gray_meta<=0;write_gray_sync<=0;head_valid<=0;
        end else begin
            write_gray_meta<=write_gray;write_gray_sync<=write_gray_meta;
            if(!head_valid && !empty) head_valid<=1;
            else if(dst_valid && dst_ready) begin
                head_valid<=0;
                read_binary<=read_next;read_gray<=(read_next>>1)^read_next;
            end
        end
    end
endmodule
