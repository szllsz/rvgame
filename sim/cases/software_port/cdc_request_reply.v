`timescale 1ns/1ps
// One ordered request/reply between unrelated clocks, using held payloads.
// Reference reviewed: pulp-platform/common_cells cc_cdc_2phase (not copied).
// reset_n asserts for BOTH endpoints together; local release is synchronized.
// CPU HOLD uses src_quiesce and drains to src_idle, never a one-sided reset.
// Physical integration must bound bundled-data delay/skew; no blanket clock-group waiver.
module cdc_request_reply #(
    parameter REQUEST_WIDTH=69,
    parameter EXTERNAL_DST_RESET=0,
    parameter RESPONSE_WIDTH=32
)(
    input wire src_clk,input wire dst_clk,input wire reset_n,
    input wire dst_domain_reset_n,
    input wire src_quiesce,output wire src_idle,output wire src_reset_ready,
    input wire src_valid,output wire src_ready,
    input wire [REQUEST_WIDTH-1:0] src_request,
    output reg src_response_valid,input wire src_response_ready,
    output reg [RESPONSE_WIDTH-1:0] src_response,
    output wire dst_valid,input wire dst_ready,
    output reg [REQUEST_WIDTH-1:0] dst_request,
    input wire [RESPONSE_WIDTH-1:0] dst_response
);
    (* ASYNC_REG="TRUE" *) reg [1:0] src_release,dst_release;
    (* ASYNC_REG="TRUE" *) reg [1:0] ack_sync,request_sync;
    reg request_toggle,ack_toggle;
    reg src_busy,dst_active;
    reg [REQUEST_WIDTH-1:0] request_payload;
    reg [RESPONSE_WIDTH-1:0] response_payload;
    wire src_reset_n=src_release[1];
    wire dst_reset_n=EXTERNAL_DST_RESET ? dst_domain_reset_n : dst_release[1];
    assign src_reset_ready=src_reset_n;
    assign src_idle=src_reset_n && !src_busy && !src_response_valid;
    assign src_ready=src_idle && !src_quiesce;
    assign dst_valid=dst_reset_n && dst_active;

    always @(posedge src_clk or negedge reset_n) begin
        if(!reset_n) src_release<=0;
        else src_release<={src_release[0],1'b1};
    end
    // In a full system every destination-domain state shares one release tree.
    // Default standalone behavior retains the private two-flop synchronizer.
    generate if(!EXTERNAL_DST_RESET) begin:g_private_dst_reset
    always @(posedge dst_clk or negedge reset_n) begin
        if(!reset_n) dst_release<=0;
        else dst_release<={dst_release[0],1'b1};
    end
    end endgenerate
    always @(posedge src_clk or negedge src_reset_n) begin
        if(!src_reset_n) begin
            ack_sync<=0;request_toggle<=0;src_busy<=0;
            request_payload<=0;src_response_valid<=0;src_response<=0;
        end else begin
            ack_sync<={ack_sync[0],ack_toggle};
            if(src_release[1]) begin
                if(src_response_valid && src_response_ready) src_response_valid<=0;
                if(src_busy && ack_sync[1]==request_toggle) begin
                    src_response<=response_payload;src_response_valid<=1;src_busy<=0;
                end
                if(src_valid && src_ready) begin
                    request_payload<=src_request;request_toggle<=~request_toggle;src_busy<=1;
                end
            end
        end
    end
    always @(posedge dst_clk or negedge dst_reset_n) begin
        if(!dst_reset_n) begin
            request_sync<=0;ack_toggle<=0;dst_active<=0;
            dst_request<=0;response_payload<=0;
        end else begin
            request_sync<={request_sync[0],request_toggle};
            if(dst_reset_n) begin
                if(!dst_active && request_sync[1]!=ack_toggle) begin
                    dst_request<=request_payload;dst_active<=1;
                end
                if(dst_valid && dst_ready) begin
                    response_payload<=dst_response;ack_toggle<=request_sync[1];dst_active<=0;
                end
            end
        end
    end
endmodule
