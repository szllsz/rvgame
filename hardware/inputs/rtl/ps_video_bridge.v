`timescale 1ns/1ps
// Control and backend participate in the SAME reset assertion. PS AXI warm reset
// must reset/drain this transport and video backend as a group, never one endpoint.
// request_valid stays asserted until response_valid; exactly one operation is owned.
module ps_video_bridge #(parameter SHARED_VIDEO_RESET=0) (
    input wire control_clk,input wire video_clk,input wire reset_n,
    input wire video_domain_reset_n,
    input wire request_valid,input wire request_write,input wire [11:0] request_address,
    input wire [31:0] request_write_data,input wire [3:0] request_write_strobe,
    output wire response_valid,output wire [31:0] response_data,output wire [1:0] response_status,
    input wire asset_busy,input wire frame_busy,input wire frame_fault,
    input wire [31:0] frame_count,input wire [31:0] game_position,
    input wire [31:0] frame_underflows,input wire [31:0] frame_copies,
    input wire [31:0] present_count,fps_frames,fps_id,fps_status,
    input wire [31:0] pcm_read_data,input wire pcm_write_ready,
    output wire pcm_write,output wire [7:0] pcm_address,output wire [31:0] pcm_data,
    output reg video_hold,output reg [31:0] asset_base,output reg [31:0] asset_size,
    output reg asset_enable,output reg [31:0] frame_base,output reg frame_enable
);
    wire sender_ready,destination_valid,destination_ready;
    wire [68:0] packet;
    wire [33:0] reply;
    wire is_write=packet[68];
    wire [3:0] strobe=packet[67:64];
    wire [11:0] address=packet[43:32];
    wire [31:0] data=packet[31:0];
    reg [31:0] read_data;
    reg [1:0] status;
    assign pcm_address=address[7:0];assign pcm_data=data;
    assign pcm_write=destination_valid && destination_ready && is_write && status==0 && address==12'h0b0;
    (* ASYNC_REG="TRUE" *) reg [1:0] video_release;
    wire video_reset_n=SHARED_VIDEO_RESET ? video_domain_reset_n : video_release[1];
    wire changes_assets=is_write && (address==12'h054 || address==12'h058 || address==12'h064);
    assign destination_ready=video_reset_n && (!(changes_assets || (is_write && address==12'h028 && data==1)) || !asset_busy);
    assign response_data=reply[31:0];assign response_status=reply[33:32];
    cdc_request_reply #(.EXTERNAL_DST_RESET(SHARED_VIDEO_RESET),.REQUEST_WIDTH(69),.RESPONSE_WIDTH(34)) u_transport(
        .src_clk(control_clk),.dst_clk(video_clk),.reset_n(reset_n),.dst_domain_reset_n(video_domain_reset_n),
        .src_quiesce(1'b0),.src_idle(),.src_reset_ready(),
        .src_valid(request_valid && !response_valid),.src_ready(sender_ready),
        .src_request({request_write,request_write_strobe,20'd0,request_address,request_write_data}),
        .src_response_valid(response_valid),.src_response_ready(request_valid),.src_response(reply),
        .dst_valid(destination_valid),.dst_ready(destination_ready),.dst_request(packet),.dst_response({status,read_data}));
    generate if(!SHARED_VIDEO_RESET) begin:g_private_video_reset
    always @(posedge video_clk or negedge reset_n) begin
        if(!reset_n) video_release<=0;
        else video_release<={video_release[0],1'b1};
    end
    end endgenerate
    always @(*) begin
        read_data=0;status=0;
        if(is_write) begin
            if(strobe!=15) status=2;
            else case(address)
                12'h028:if(data>1) status=2;
                12'h054:if(!video_hold || data[5:0]!=0 || data>=32'h40000000) status=2;
                12'h058:if(!video_hold || data>1 || (data[0] && (asset_size<64 || asset_base>32'h40000000-asset_size))) status=2;
                12'h064:if(!video_hold || data[5:0]!=0 || data<64 || data>32'h01000000) status=2;
                12'h06c:if(!video_hold || frame_enable || frame_busy || data[5:0]!=0 || data>32'h3ff00000) status=2;
                12'h070:if(!video_hold || data>1 || (data[0] && frame_busy)) status=2;
                12'h0b0:if(video_hold || !pcm_write_ready) status=2;
                default:status=2;
            endcase
        end else case(address)
            12'h010:read_data=frame_count;
            12'h048:read_data=game_position;
            12'h06c:read_data=frame_base;
            12'h070:read_data={31'd0,frame_enable};
            12'h074:read_data={30'd0,frame_busy,frame_fault};
            12'h078:read_data=frame_underflows;
            12'h07c:read_data=frame_copies;
            // New addresses; 0x7c remains DDR copy completion, never redefined.
            12'h080:read_data=present_count;
            12'h084:read_data=fps_frames;
            12'h088:read_data=fps_id;
            12'h08c:read_data=fps_status;
            12'h0b4,12'h0b8,12'h0bc,12'h0c0,12'h0c4,12'h0c8,12'h0cc,
            12'h0d0,12'h0d4,12'h0d8,12'h0dc:read_data=pcm_read_data;
            default:status=2;
        endcase
    end
    // BRAM-facing addresses/enables change only at a video-clock edge.
    always @(posedge video_clk) begin
        if(!video_reset_n) begin
            video_hold<=0;asset_base<=0;asset_size<=0;asset_enable<=0;frame_base<=0;frame_enable<=0;
        end else if(destination_valid && destination_ready && is_write && status==0) begin
            case(address)
                12'h028:video_hold<=data[0];
                12'h054:asset_base<=data;
                12'h058:asset_enable<=data[0];
                12'h064:asset_size<=data;
                12'h06c:frame_base<=data;
                12'h070:frame_enable<=data[0];
                default:begin end
            endcase
        end
    end
endmodule
