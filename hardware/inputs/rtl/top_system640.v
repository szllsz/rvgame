module top (
    inout wire [14:0] DDR_addr,
    inout wire [2:0] DDR_ba,
    inout wire DDR_cas_n,
    inout wire DDR_ck_n,
    inout wire DDR_ck_p,
    inout wire DDR_cke,
    inout wire DDR_cs_n,
    inout wire [3:0] DDR_dm,
    inout wire [31:0] DDR_dq,
    inout wire [3:0] DDR_dqs_n,
    inout wire [3:0] DDR_dqs_p,
    inout wire DDR_odt,
    inout wire DDR_ras_n,
    inout wire DDR_reset_n,
    inout wire DDR_we_n,
    inout wire FIXED_IO_ddr_vrn,
    inout wire FIXED_IO_ddr_vrp,
    inout wire [53:0] FIXED_IO_mio,
    inout wire FIXED_IO_ps_clk,
    inout wire FIXED_IO_ps_porb,
    inout wire FIXED_IO_ps_srstb,
    input wire sys_clk,
    input wire rst_n,
    input wire [2:0] key_n,
    input wire hdmi_hpd,
    output wire [3:0] led,
    output wire hdmi_oen,
    output wire TMDS_clk_n,
    output wire TMDS_clk_p,
    output wire [2:0] TMDS_data_n,
    output wire [2:0] TMDS_data_p,
    output wire audio_din,audio_bclk,audio_lrc
);
  wire sys_clk_buffered;
  IBUF u_sys_clk_input(.I(sys_clk),.O(sys_clk_buffered));
  wire pixel_clk, serial_clk, locked;
  wire cpu_clk,cpu_locked,common_reset_n,cpu_quiescent;
  wire video_request,video_write,video_ready;
  wire [11:0] video_address;
  wire [7:0] pcm_host_address;
  wire [31:0] pcm_host_data,pcm_host_read_data;
  wire pcm_host_write,pcm_host_ready;
  wire [31:0] video_write_data,video_read_data;
  wire [3:0] video_write_strobe;
  wire [1:0] video_response;
  wire video_hold,video_asset_enable;
  wire [31:0] video_asset_base,video_asset_size;
  cpu_clock u_cpu_clock(.clk_in1(sys_clk_buffered),.clk_out1(cpu_clk),.reset(1'b0),.locked(cpu_locked));
  (* ASYNC_REG="TRUE" *) reg [1:0] reset_sync = 0;
  wire release_reset = common_reset_n;
  always @(posedge pixel_clk or negedge release_reset) begin
    if (!release_reset) reset_sync <= 0;
    else reset_sync <= {reset_sync[0], 1'b1};
  end
  video_clock u_video_clock (
      .clk_in1(sys_clk_buffered),
      .clk_out1(pixel_clk),
      .clk_out2(serial_clk),
      .reset(1'b0),
      .locked(locked)
  );
  wire base_hs, base_vs, base_de, hs, vs, de;
  wire [23:0] rgb;
  wire [2:0] buttons_pressed;
  wire [2:0] host_buttons;
  wire host_enable;
  wire [11:0] gamepad_buttons;
  wire gamepad_connected;
  wire [31:0] gamepad_left,gamepad_right,gamepad_aux,gamepad_sequence;
  wire [2:0] hdmi_detect;
  wire [3:0] debug_led;
  wire [31:0] game_position;
  assign led = {debug_led[3:2], ~gamepad_connected, ~hdmi_detect[0]};
  wire cpu_hold, load_write;
  wire [31:0] load_address, load_data, load_read_data;
  wire [31:0] ps_frame_count, ps_cpu_pc;
  wire [31:0] ddr_base, ddr_size, ddr_araddr, ddr_rdata;
  wire ddr_enable, ddr_flush, ddr_busy, ddr_fault;
  wire ddr_arvalid, ddr_arready, ddr_rvalid, ddr_rready, ddr_rlast;
  wire [7:0] ddr_arlen;
  wire [2:0] ddr_arsize, ddr_arprot;
  wire [1:0] ddr_arburst, ddr_rresp;
  wire [3:0] ddr_arcache;
  wire [31:0] frame_base;
  wire frame_enable;
  wire frame_busy;
  wire frame_fault;
  wire [31:0] frame_underflows;
  wire [31:0] frame_copies;
  wire [31:0] ps_present_count,ps_fps_frames,ps_fps_id,ps_fps_status;
  wire [31:0] frame_araddr;
  wire [3:0] frame_arlen;
  wire frame_arvalid;
  wire frame_arready;
  wire [31:0] frame_rdata;
  wire [1:0] frame_rresp;
  wire frame_rlast;
  wire frame_rvalid;
  wire frame_rready;
  wire [31:0] frame_awaddr;
  wire [3:0] frame_awlen;
  wire frame_awvalid;
  wire frame_awready;
  wire [31:0] frame_wdata;
  wire [3:0] frame_wstrb;
  wire frame_wlast;
  wire frame_wvalid;
  wire frame_wready;
  wire [1:0] frame_bresp;
  wire frame_bvalid;
  wire frame_bready;
  ps_sys_wrapper u_ps (
.frame_base(),
.frame_enable(),
.frame_busy(1'b0),
.frame_fault(1'b0),
.frame_underflows(32'd0),
.frame_copies(32'd0),
.hp1_araddr(frame_araddr),
.hp1_arlen(frame_arlen),
.hp1_arsize(3'd2),
.hp1_arburst(2'b01),
.hp1_arcache(4'b0011),
.hp1_arid(6'd0),
.hp1_arlock(2'b00),
.hp1_arprot(3'd0),
.hp1_arqos(4'd0),
.hp1_arvalid(frame_arvalid),
.hp1_arready(frame_arready),
.hp1_awaddr(frame_awaddr),
.hp1_awlen(frame_awlen),
.hp1_awsize(3'd2),
.hp1_awburst(2'b01),
.hp1_awcache(4'b0011),
.hp1_awid(6'd0),
.hp1_awlock(2'b00),
.hp1_awprot(3'd0),
.hp1_awqos(4'd0),
.hp1_awvalid(frame_awvalid),
.hp1_awready(frame_awready),
.hp1_rdata(frame_rdata),
.hp1_rid(),
.hp1_rresp(frame_rresp),
.hp1_rlast(frame_rlast),
.hp1_rvalid(frame_rvalid),
.hp1_rready(frame_rready),
.hp1_wdata(frame_wdata),
.hp1_wid(6'd0),
.hp1_wstrb(frame_wstrb),
.hp1_wlast(frame_wlast),
.hp1_wvalid(frame_wvalid),
.hp1_wready(frame_wready),
.hp1_bid(),
.hp1_bresp(frame_bresp),
.hp1_bvalid(frame_bvalid),
.hp1_bready(frame_bready),
      .DDR_addr(DDR_addr),
      .DDR_ba(DDR_ba),
      .DDR_cas_n(DDR_cas_n),
      .DDR_ck_n(DDR_ck_n),
      .DDR_ck_p(DDR_ck_p),
      .DDR_cke(DDR_cke),
      .DDR_cs_n(DDR_cs_n),
      .DDR_dm(DDR_dm),
      .DDR_dq(DDR_dq),
      .DDR_dqs_n(DDR_dqs_n),
      .DDR_dqs_p(DDR_dqs_p),
      .DDR_odt(DDR_odt),
      .DDR_ras_n(DDR_ras_n),
      .DDR_reset_n(DDR_reset_n),
      .DDR_we_n(DDR_we_n),
      .FIXED_IO_ddr_vrn(FIXED_IO_ddr_vrn),
      .FIXED_IO_ddr_vrp(FIXED_IO_ddr_vrp),
      .FIXED_IO_mio(FIXED_IO_mio),
      .FIXED_IO_ps_clk(FIXED_IO_ps_clk),
      .FIXED_IO_ps_porb(FIXED_IO_ps_porb),
      .FIXED_IO_ps_srstb(FIXED_IO_ps_srstb),
      .pixel_clk(pixel_clk),
      .reset_n(rst_n && locked && cpu_locked),
      .cpu_clk(cpu_clk), .pl_reset_n(common_reset_n),
      .cpu_quiescent(cpu_quiescent), .video_request(video_request), .video_write(video_write),
      .video_address(video_address), .video_write_data(video_write_data), .video_write_strobe(video_write_strobe),
      .video_ready(video_ready), .video_read_data(video_read_data), .video_response(video_response),
      .frame_count(32'd0),
      .cpu_pc(ps_cpu_pc),
      .board_buttons(buttons_pressed),
      .host_buttons(host_buttons),
      .host_enable(host_enable),
      .gamepad_buttons(gamepad_buttons),
      .gamepad_connected(gamepad_connected),
      .gamepad_left(gamepad_left),.gamepad_right(gamepad_right),
      .gamepad_aux(gamepad_aux),.gamepad_sequence(gamepad_sequence),
      .hdmi_connected(hdmi_detect[0]),
      .game_position(32'd0),
      .cpu_hold(cpu_hold),
      .load_write(load_write),
      .load_address(load_address),
      .load_data(load_data),
      .load_read_data(load_read_data),
      .ddr_base(ddr_base),
      .ddr_size(ddr_size),
      .ddr_enable(ddr_enable),
      .ddr_flush(ddr_flush),
      .ddr_busy(ddr_busy),
      .ddr_fault(ddr_fault),
      // HP0为AXI3；当前只用16拍INCR读，ARLEN低4位与缓存固定15一致。
      .hp0_araddr(ddr_araddr),
      .hp0_arburst(ddr_arburst),
      .hp0_arcache(ddr_arcache),
      .hp0_arid(6'd0),
      .hp0_arlen(ddr_arlen[3:0]),
      .hp0_arlock(2'b00),
      .hp0_arprot(ddr_arprot),
      .hp0_arqos(4'd0),
      .hp0_arready(ddr_arready),
      .hp0_arsize(ddr_arsize),
      .hp0_arvalid(ddr_arvalid),
      .hp0_rdata(ddr_rdata),
      .hp0_rid(),
      .hp0_rlast(ddr_rlast),
      .hp0_rready(ddr_rready),
      .hp0_rresp(ddr_rresp),
      .hp0_rvalid(ddr_rvalid),
      .hp0_awaddr(32'd0),
      .hp0_awburst(2'b01),
      .hp0_awcache(4'd0),
      .hp0_awid(6'd0),
      .hp0_awlen(4'd0),
      .hp0_awlock(2'b00),
      .hp0_awprot(3'd0),
      .hp0_awqos(4'd0),
      .hp0_awready(),
      .hp0_awsize(3'd2),
      .hp0_awvalid(1'b0),
      .hp0_bid(),
      .hp0_bready(1'b1),
      .hp0_bresp(),
      .hp0_bvalid(),
      .hp0_wdata(32'd0),
      .hp0_wid(6'd0),
      .hp0_wlast(1'b0),
      .hp0_wready(),
      .hp0_wstrb(4'd0),
      .hp0_wvalid(1'b0)
  );
  button_input #(.DEBOUNCE_CYCLES(1050000)) u_buttons (
      .clk(cpu_clk),
      .reset_n(common_reset_n),
      .key_n(key_n),
      .pressed(buttons_pressed)
  );
  // HPD为异步高有效，复用10ms按键同步/消抖；另外两路恒定未按下。
  button_input #(.DEBOUNCE_CYCLES(1050000)) u_hdmi_detect (
      .clk(cpu_clk),
      .reset_n(common_reset_n),
      .key_n({2'b11, ~hdmi_hpd}),
      .pressed(hdmi_detect)
  );
  color_bar #(.H_FP(16'd1500)) u_timing (
      .clk(pixel_clk),
      .rst(!reset_sync[1]),
      .hs(base_hs),
      .vs(base_vs),
      .de(base_de),
      .rgb_r(),
      .rgb_g(),
      .rgb_b()
  );
  ps_video_bridge #(.SHARED_VIDEO_RESET(1)) u_ps_video(.video_domain_reset_n(reset_sync[1]),.control_clk(cpu_clk),.video_clk(pixel_clk),.reset_n(common_reset_n),
      .pcm_address(pcm_host_address),.pcm_write(pcm_host_write),.pcm_data(pcm_host_data),
      .pcm_read_data(pcm_host_read_data),.pcm_write_ready(pcm_host_ready),
      .request_valid(video_request),.request_write(video_write),.request_address(video_address),
      .request_write_data(video_write_data),.request_write_strobe(video_write_strobe),
      .response_valid(video_ready),.response_data(video_read_data),.response_status(video_response),
      .asset_busy(frame_busy),.frame_busy(frame_busy),.frame_fault(frame_fault),
      .frame_count(ps_frame_count),.game_position(game_position),.frame_underflows(frame_underflows),.frame_copies(frame_copies),
      .present_count(ps_present_count),.fps_frames(ps_fps_frames),.fps_id(ps_fps_id),.fps_status(ps_fps_status),
      .video_hold(video_hold),.asset_base(video_asset_base),.asset_size(video_asset_size),.asset_enable(video_asset_enable),
      .frame_base(frame_base),.frame_enable(frame_enable));
  graphics_soc #(
      .MACHINE_TIMER_ENABLE(1),
      .INDEPENDENT_CLOCKS(1), .PERIPHERAL_CLOCK_HZ(105000000),
      .DDR_REGISTER_CONTROL_FETCH(1),
      .DDR_FALLTHROUGH_FETCH(1),
      .GPU_REGISTER_TILE_START(1),
      .GPU_REGISTER_DRAW_START(1),
      .DDR_FAST_ADDRESS_REGIONS(1),
      .DDR_SPLIT_INSTRUCTION_BOUNDS(1),
      .DDR_REGISTER_IMMEDIATE(1),
      .DDR_DECOUPLED_READ_HINT(1),
      .SYSTEM_CLOCK_HZ(105000000),
      .AUDIO_SERIAL_FRAME_MEAN(1),
      .USE_C_PROGRAM(1),  // C程序上传素材并绘制四种翻转方向
      .AUDIO_ENABLE(1), // 统一640x480图形与I2S音频
      .DDR_BITMAP(1), // 独立位图试验：DDR素材不占额外BRAM
      .DDR_TILE_ROW(1), // 独立640x480试验：按行预取，默认正式版保持原路径
      .NATIVE800(1),
      .DDR_FRAME(1),
      .COMPACT_FRAME(1),  // 256x240双帧存，显示端居中2倍放大
      .HOST_LOADER(1),  // PS先复位CPU，再加载96 KiB程序
      .GAMEPAD_INPUT(1),
      .SHOW_FPS(1),
      .DDR_PROGRAM(1),  // DDR默认关闭，旧马里奥保持96 KiB BRAM程序路径
      .DDR_CACHE_LINES(1024),  // 64 KiB；覆盖较大代码/只读数据工作集，减轻容量miss
      .DDR_CACHE_WAYS(4),  // 四路并行读取，缓解指令/数据同组冲突
      .DDR_FETCH_OVERLAP(1),  // 直线ALU译码时启动下一条DDR取指
      .DDR_CONTROL_OVERLAP(1),  // 按实际分支/跳转目标启动取指
      .DDR_MEMORY_OVERLAP(1),  // Store提交/Load写回与下一条DDR取指重叠
      .DDR_LOCAL_LOAD_OVERLAP(1),  // 本地Load与DDR取指并行，在数据ready拍写回
      .DDR_DECODE_CONTROL(1),  // 译码拍读取当前寄存器值，提前解析实际目标
      .DDR_EARLY_HIT(1),  // 同步BRAM在LOOKUP拍返回命中，未命中仍等待整行填充
      .DDR_FETCH_FOLD(1),  // 当前指令提交且下一条已返回时直接进入DECODE
      .DDR_DECODE_RAM_LOAD(1),  // 仅工作RAM在译码拍给地址，EXECUTE拍按ready写回
      .DDR_SYNC_RESET(1),  // CPU/寄存器堆同步复位，复位期间立即禁止提交
      .DDR_DECODE_DDR_LOAD(1),  // DDR Load译码拍发出，命中时EXECUTE直接写回
      .DDR_FAST_STORE(1),  // 本地Store在EXECUTE提交，保留SoC寄存后写RAM
      .DDR_STORE_DIRECT_ADDR(1),  // Store独立地址加法器，缩短通用ALU选择路径
      .DDR_LOAD_HANDOFF(1),  // DDR Load返回沿把共享缓存交接给下一条取指
      .DDR_HANDOFF_HINT(1),  // 独立next接口提前给缓存读地址，旧响应成功后才交接
      .DDR_INSTRUCTION_BUFFER_WORDS(1024),  // 4 KiB只读指令缓冲，hold/flush/reset清除
      .DDR_FAST_DECODE(1),  // 整数/控制指令译码拍提交，缓冲命中时连续执行
      .DDR_DECODE_STORE(1),  // 本地Store译码拍提交；RAM字节转发，像素端口按ready等待
      .DDR_INSTRUCTION_LINE_FILL(1)  // 指令AXI整行成功后发布16字，错误/flush禁止整行发布
  ) u_soc (
      .audio_din(audio_din),.audio_bclk(audio_bclk),.audio_lrc(audio_lrc),
.frame_base(frame_base),
.frame_enable(frame_enable),
.frame_busy(frame_busy),
.frame_fault(frame_fault),
.frame_underflows(frame_underflows),
.frame_copies(frame_copies),
.frame_araddr(frame_araddr),
.frame_arlen(frame_arlen),
.frame_arvalid(frame_arvalid),
.frame_arready(frame_arready),
.frame_rdata(frame_rdata),
.frame_rresp(frame_rresp),
.frame_rlast(frame_rlast),
.frame_rvalid(frame_rvalid),
.frame_rready(frame_rready),
.frame_awaddr(frame_awaddr),
.frame_awlen(frame_awlen),
.frame_awvalid(frame_awvalid),
.frame_awready(frame_awready),
.frame_wdata(frame_wdata),
.frame_wstrb(frame_wstrb),
.frame_wlast(frame_wlast),
.frame_wvalid(frame_wvalid),
.frame_wready(frame_wready),
.frame_bresp(frame_bresp),
.frame_bvalid(frame_bvalid),
.frame_bready(frame_bready),
      .clk(cpu_clk),
      .reset_n(common_reset_n), .video_clk(pixel_clk), .video_reset_n(reset_sync[1]), .common_reset_n(common_reset_n),
      .video_cpu_hold(video_hold), .video_asset_base(video_asset_base), .video_asset_size(video_asset_size),
      .video_asset_enable(video_asset_enable), .cpu_quiescent(cpu_quiescent),
      .pcm_host_address(pcm_host_address),.pcm_host_write(pcm_host_write),.pcm_host_data(pcm_host_data),
      .pcm_host_ready(pcm_host_ready),.pcm_host_read_data(pcm_host_read_data),
      .cpu_hold(cpu_hold),
      .load_write(load_write),
      .load_address(load_address),
      .load_data(load_data),
      .load_read_data(load_read_data),
      .buttons_pressed(host_enable ? host_buttons : buttons_pressed),
      .gamepad_buttons(gamepad_buttons),
      .gamepad_connected(gamepad_connected),
      .gamepad_left(gamepad_left),.gamepad_right(gamepad_right),
      .gamepad_aux(gamepad_aux),.gamepad_sequence(gamepad_sequence),
      .hs_in(base_hs),
      .vs_in(base_vs),
      .de_in(base_de),
      .hs(hs),
      .vs(vs),
      .de(de),
      .rgb(rgb),
      .led(debug_led),
      .ps_frame_count(ps_frame_count),
      .ps_cpu_pc(ps_cpu_pc),
      .ps_game_position(game_position),
      .ps_present_count(ps_present_count),.ps_fps_frames(ps_fps_frames),.ps_fps_id(ps_fps_id),.ps_fps_status(ps_fps_status),
      .ddr_base(ddr_base),
      .ddr_size(ddr_size),
      .ddr_enable(ddr_enable),
      .ddr_flush(ddr_flush),
      .ddr_busy(ddr_busy),
      .ddr_fault(ddr_fault),
      .ddr_araddr(ddr_araddr),
      .ddr_arlen(ddr_arlen),
      .ddr_arsize(ddr_arsize),
      .ddr_arburst(ddr_arburst),
      .ddr_arcache(ddr_arcache),
      .ddr_arprot(ddr_arprot),
      .ddr_arvalid(ddr_arvalid),
      .ddr_arready(ddr_arready),
      .ddr_rdata(ddr_rdata),
      .ddr_rresp(ddr_rresp),
      .ddr_rlast(ddr_rlast),
      .ddr_rvalid(ddr_rvalid),
      .ddr_rready(ddr_rready)
  );
  rgb2dvi_0 u_hdmi (
      .TMDS_Clk_p(TMDS_clk_p),
      .TMDS_Clk_n(TMDS_clk_n),
      .TMDS_Data_p(TMDS_data_p),
      .TMDS_Data_n(TMDS_data_n),
      .oen(hdmi_oen),
      .aRst_n(reset_sync[1]),
      .vid_pData(rgb),
      .vid_pVDE(de),
      .vid_pHSync(hs),
      .vid_pVSync(vs),
      .PixelClk(pixel_clk),
      .SerialClk(serial_clk)
  );
endmodule
