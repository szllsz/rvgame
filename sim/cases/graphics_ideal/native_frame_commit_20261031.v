`timescale 1ns/1ps
// Fixed two-band transaction; descriptors are accepted only at the requested band.
// Public frames count only complete frames at a frame edge, never DMA bands.
module native_frame_commit (
    input wire clk,reset_n,enable,begin_frame,band_valid,band_id,frame_edge,abort_frame,
    input wire copy_done,copy_busy,bus_busy,copy_fault,
    output reg copy_start,copy_band,copy_target_bank,
    output reg front,display_valid,
    output wire need_band,active,ready_to_present,presented,
    output reg failed,
    output reg [31:0] completed_frames
);
    localparam IDLE=0,LOAD0=1,WAIT0=2,LOAD1=3,WAIT1=4,READY=5,DRAIN=6;
    reg [2:0] state;
    assign need_band=state==LOAD0 || state==LOAD1;
    assign active=state!=IDLE;
    assign ready_to_present=state==READY && !copy_busy && !copy_fault && !failed;
    assign presented=reset_n && enable && !abort_frame && ready_to_present && frame_edge;
    always @(posedge clk) begin
        if(!reset_n) begin
            state<=IDLE;copy_start<=0;copy_band<=0;copy_target_bank<=0;
            front<=0;display_valid<=0;failed<=0;completed_frames<=0;
        end else begin
            copy_start<=0;
            if(!enable && state==IDLE && !bus_busy) failed<=0;
            if(state!=IDLE && (!enable || abort_frame || copy_fault)) begin
                failed<=1;
                if(!copy_busy && !copy_start && !bus_busy)state<=IDLE;
                else state<=DRAIN;
            end else case(state)
                IDLE: if(begin_frame && enable && !bus_busy && !failed && !copy_fault)begin
                    copy_target_bank<=!front;copy_band<=0;state<=LOAD0;
                end
                LOAD0,LOAD1: if(band_valid)begin
                    if(band_id!=copy_band)begin failed<=1;state<=DRAIN;end
                    else begin copy_start<=1;state<=state==LOAD0 ? WAIT0 : WAIT1;end
                end
                WAIT0: if(copy_done && !copy_busy)begin copy_band<=1;state<=LOAD1;end
                WAIT1: if(copy_done && !copy_busy)state<=READY;
                READY: if(presented)begin
                    front<=copy_target_bank;display_valid<=1;
                    completed_frames<=completed_frames+1'b1;state<=IDLE;
                end
                DRAIN: if(!copy_busy && !copy_start && !bus_busy)state<=IDLE;
                default:begin failed<=1;state<=DRAIN;end
            endcase
        end
    end
endmodule
