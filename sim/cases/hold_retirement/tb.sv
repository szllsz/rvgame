`timescale 1ns/1ps
module native_hold_case #(parameter ABORT_FIRST=0, DMA_BUSY=0);
  reg clk=0,video_clk=0,reset_n=0;
  always #4.762 clk=~clk;
  initial begin #1.3;forever #4.762 video_clk=~video_clk;end
  reg [1:0] release_video=0;
  always @(posedge video_clk or negedge reset_n)
    if(!reset_n)release_video<=0;else release_video<={release_video[0],1'b1};
  reg [11:0] awaddr=0;reg [31:0] wdata=0;
  reg awvalid=0,wvalid=0,bready=0;
  wire awready,wready,bvalid,arready,cpu_hold;
  wire [1:0] bresp;
  wire video_request,video_write,video_ready;
  wire [11:0] video_address;wire [31:0] video_wdata,video_rdata;
  wire [3:0] video_strobe;wire [1:0] video_response;
  wire video_hold,video_frame_enable;
  reg begin_frame=0,abort_frame=0,band_valid=0,copy_busy=0;
  wire native_active,native_failed,copy_start;wire [31:0] completed_frames;
  native_frame_commit commit(.clk(video_clk),.reset_n(release_video[1]),.enable(video_frame_enable),
    .begin_frame(begin_frame),.band_valid(band_valid),.band_id(1'b0),.frame_edge(1'b0),.abort_frame(abort_frame),
    .copy_done(1'b0),.copy_busy(copy_busy),.bus_busy(1'b0),.copy_fault(1'b0),.copy_start(copy_start),
    .copy_band(),.copy_target_bank(),.front(),.display_valid(),.need_band(),.active(native_active),
    .ready_to_present(),.presented(),.failed(native_failed),.completed_frames(completed_frames));
  ps_control #(.VIDEO_CDC(1),.FRAME_WIDTH(720),.FRAME_HEIGHT(540)) control(
    .s_axi_aclk(clk),.s_axi_aresetn(reset_n),
    .s_axi_awaddr(awaddr),.s_axi_awprot(3'd0),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
    .s_axi_wdata(wdata),.s_axi_wstrb(4'hf),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
    .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
    .s_axi_araddr(12'd0),.s_axi_arprot(3'd0),.s_axi_arvalid(1'b0),.s_axi_arready(arready),
    .s_axi_rdata(),.s_axi_rresp(),.s_axi_rvalid(),.s_axi_rready(1'b0),.cpu_quiescent(1'b1),
    .video_request(video_request),.video_write(video_write),.video_address(video_address),
    .video_write_data(video_wdata),.video_write_strobe(video_strobe),.video_ready(video_ready),
    .video_read_data(video_rdata),.video_response(video_response),
    .frame_count(32'd0),.cpu_pc(32'd0),.board_buttons(3'd0),.host_buttons(),.host_enable(),
    .hdmi_connected(1'b1),.game_position(32'd0),.gamepad_buttons(),.gamepad_connected(),
    .gamepad_left(),.gamepad_right(),.gamepad_aux(),.gamepad_sequence(),.cpu_hold(cpu_hold),
    .load_address(),.load_data(),.load_write(),.load_read_data(32'd0),
    .ddr_base(),.ddr_size(),.ddr_enable(),.ddr_flush(),.ddr_busy(1'b0),.ddr_fault(1'b0),
    .frame_base(),.frame_enable(),.frame_busy(native_active),.frame_fault(native_failed),
    .frame_underflows(32'd0),.frame_copies(completed_frames));
  ps_video_bridge #(.SHARED_VIDEO_RESET(1)) bridge(
    .control_clk(clk),.video_clk(video_clk),.reset_n(reset_n),.video_domain_reset_n(release_video[1]),
    .request_valid(video_request),.request_write(video_write),.request_address(video_address),
    .request_write_data(video_wdata),.request_write_strobe(video_strobe),
    .response_valid(video_ready),.response_data(video_rdata),.response_status(video_response),
    .asset_busy(native_active),.frame_busy(native_active),.frame_fault(native_failed),
    .frame_count(32'd0),.game_position(32'd0),.frame_underflows(32'd0),.frame_copies(completed_frames),
    .present_count(32'd0),.fps_frames(32'd0),.fps_id(32'd0),.fps_status(32'd0),
    .pcm_read_data(32'd0),.pcm_write_ready(1'b1),.pcm_write(),.pcm_address(),.pcm_data(),
    .video_hold(video_hold),.asset_base(),.asset_size(),.asset_enable(),.frame_base(),.frame_enable(video_frame_enable));
  task send_write(input [11:0] a,input [31:0] d);
    begin
      @(negedge clk);awaddr=a;wdata=d;awvalid=1;wvalid=1;
      @(posedge clk);while(!(awready&&wready))@(posedge clk);
      @(negedge clk);awvalid=0;wvalid=0;
    end
  endtask
  task finish_write;
    integer ticks;
    begin
      ticks=0;
      while(!bvalid)begin @(negedge clk);ticks=ticks+1;if(ticks>300)$fatal(1,"RECOVERY_HOLD_DID_NOT_REPLY");end
      if(bresp!==0)$fatal(1,"RECOVERY_BAD_AXI_RESPONSE");
      bready=1;@(posedge clk);@(negedge clk);bready=0;
    end
  endtask
  task wr(input [11:0] a,input [31:0] d);begin send_write(a,d);finish_write();end endtask
  initial begin
    repeat(6)@(negedge clk);reset_n=1;wait(!control.recover_active);repeat(12)@(negedge clk);
    wr(12'h028,1);wr(12'h06c,32'h10000000);wr(12'h070,1);wr(12'h028,0);
    if(cpu_hold||video_hold||!video_frame_enable)$fatal(1,"NATIVE_HOLD_SETUP");
    @(negedge video_clk);begin_frame=1;@(negedge video_clk);begin_frame=0;
    wait(native_active);
    if(DMA_BUSY)begin
      @(negedge video_clk);band_valid=1;@(negedge video_clk);band_valid=0;copy_busy=1;
    end
    if(ABORT_FIRST)begin
      @(negedge video_clk);abort_frame=1;@(negedge video_clk);abort_frame=0;
      if(DMA_BUSY)begin
        repeat(8)@(negedge video_clk);
        if(!native_active)$fatal(1,"ABORT_FREED_BEFORE_COPY_DRAIN");
        copy_busy=0;
      end
      wait(!native_active);
      if(!native_failed||completed_frames)$fatal(1,"ABORT_HID_ERROR_OR_PUBLISHED_PARTIAL_FRAME");
      send_write(12'h028,1);finish_write();
      if(!cpu_hold||!video_hold||!native_failed)$fatal(1,"ABORT_HOLD_STATE");
      $display("PASS NATIVE_ABORT_BEFORE_HOLD dma_busy=%0d fault_retained=1 partial_frames=0 AXI_replied=1",DMA_BUSY);
    end else begin
      send_write(12'h028,1);repeat(300)@(negedge clk);
      if(!cpu_hold||!native_active||bvalid||arready||video_hold||!control.video_pending)
        $fatal(1,"EXPECTED_HOLD_DEADLOCK_NOT_REPRODUCED");
      $display("PASS REPRODUCED_NATIVE_HOLD_DEADLOCK cpu_stopped=1 frame_active=1 AXI_B_missing=1 AXI_reads_blocked=1");
    end
    $finish;
  end
  initial begin #1000000;$fatal(1,"NATIVE_HOLD_TEST_TIMEOUT");end
endmodule
module tb_native_hold_old;native_hold_case #(.ABORT_FIRST(0)) t();endmodule
module tb_native_hold_abort;native_hold_case #(.ABORT_FIRST(1)) t();endmodule
module tb_native_hold_abort_dma;native_hold_case #(.ABORT_FIRST(1),.DMA_BUSY(1)) t();endmodule
