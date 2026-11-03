`timescale 1ns / 1ps
// RGB332, 512x300, two independent BRAM banks. One fill pixel per clock.
// Legacy FILL/COPY use fully contained unsigned rectangles; TILE clips signed 8x8 destinations.
module framebuffer_engine #(
    parameter DDR_BITMAP = 0, // DDR RGB332位图搬运；正式版默认关闭
    parameter FAST_TILE_ROW = 0, // DDR模式每行预读后连续写8像素；默认保留旧路径
    parameter COMPACT_FRAME = 0,  // 0保留512x300；1使用256x240并居中2倍显示
    parameter REGISTER_DRAW_START = 0,
    parameter REGISTER_TILE_START = 0,
    parameter NATIVE800 = 0,
    parameter DDR_FRAME = 0  // 1使用480x270单绘图BRAM和HP1 DDR双显示缓冲
) (
    input wire clk,
    input wire reset_n,
    input wire frame_edge,
    input wire [31:0] address,
    input wire [31:0] write_data,
    input wire [3:0] write_strobe,
    input wire write_enable,
    output reg [31:0] read_data,
    input wire scan_de,
    input wire [10:0] scan_x,
    input wire [9:0] scan_y,
    output wire [23:0] scan_rgb,
    output wire frame_presented,
    output wire write_ready,
    input wire [31:0] asset_base, asset_size,
    input wire asset_enable,
    input wire [31:0] frame_base,
    input wire frame_enable,
    output wire frame_busy,
    output wire frame_fault,
    output wire [31:0] frame_underflows,
    output wire [31:0] frame_copies,
    output wire [31:0] frame_araddr,
    output wire [3:0] frame_arlen,
    output wire frame_arvalid,
    input wire frame_arready,
    input wire [31:0] frame_rdata,
    input wire [1:0] frame_rresp,
    input wire frame_rlast,
    input wire frame_rvalid,
    output wire frame_rready,
    output wire [31:0] frame_awaddr,
    output wire [3:0] frame_awlen,
    output wire frame_awvalid,
    input wire frame_awready,
    output wire [31:0] frame_wdata,
    output wire [3:0] frame_wstrb,
    output wire frame_wlast,
    output wire frame_wvalid,
    input wire frame_wready,
    input wire [1:0] frame_bresp,
    input wire frame_bvalid,
    output wire frame_bready
);
  localparam FRAME_WIDTH = DDR_FRAME ? (NATIVE800 ? 800 : 640) : COMPACT_FRAME ? 256 : 512;
  localparam FRAME_HEIGHT = DDR_FRAME ? (NATIVE800 ? 300 : 480) : COMPACT_FRAME ? 240 : 300;
  (* ram_style="block" *)reg [7:0] bank0[0:FRAME_WIDTH*FRAME_HEIGHT-1];
  (* ram_style="block" *)reg [7:0] bank1[0:FRAME_WIDTH*FRAME_HEIGHT-1];
  (* ram_style="block" *) reg [31:0] dma_words0 [0:32767];
  (* ram_style="block" *) reg [31:0] dma_words1 [0:32767];
  // Exact 11264-word tail: split powers of two prevent depth rounding.
  (* ram_style="block" *) reg [31:0] dma_words2a [0:8191];
  (* ram_style="block" *) reg [31:0] dma_words2b [0:2047];
  (* ram_style="block" *) reg [31:0] dma_words2c [0:1023];
  reg [7:0] read0, read1;
  reg front, front_d, valid_d;
  reg valid0, valid1, busy, swap_pending, error;
  reg [9:0] cfg_x;
  reg [8:0] cfg_y;
  reg [9:0] cfg_w;
  reg [8:0] cfg_h;
  reg [7:0] cfg_color, fill_color;
  reg [18:0] fill_address, row_address;
  reg [9:0] fill_width, remaining_x;
  reg [8:0] remaining_y;
  reg full_clear;
  reg staging_initialized;
  reg [31:0] pixel_count, command_count, swap_count;
  reg [4:0] cfg_source_x, cfg_source_y;
  reg cfg_transparent;
  reg [7:0] cfg_key, copy_key;
  reg copy_mode, copy_phase, copy_transparent;
  reg tile_row_wait;
  reg blit_mode, blit_start;
  reg [31:0] cfg_bitmap_offset;
  reg [15:0] cfg_bitmap_stride;
  reg [2:0] cfg_bitmap_alpha;
  wire blit_done,blit_busy,blit_fault,blit_pixel_valid;
  wire [18:0] blit_pixel_address;
  wire [7:0] blit_pixel_color;
  wire [31:0] blit_written,blit_skipped,blit_clipped;
  wire [31:0] scan_araddr,blit_araddr;
  wire [3:0] scan_arlen,blit_arlen;
  wire scan_arvalid,scan_arready,scan_rvalid,scan_rready;
  wire blit_arvalid,blit_arready,blit_rvalid,blit_rready;
  wire scan_bus_busy,read_mux_busy;
  wire native_copy_start,native_copy_band,native_target,native_front,native_valid;
  wire native_need_band,native_active,native_ready,native_presented,native_failed;
  wire [31:0] native_frames,band_copies;
  wire dma_fault,dma_done,dma_busy;
  wire native_blocked=NATIVE800 && (!native_need_band || dma_busy || native_copy_start);
  assign frame_busy=scan_bus_busy || blit_busy || read_mux_busy || (NATIVE800 && native_active);
  reg [9:0] source_address, source_row_address;
  reg [31:0] skipped_count;
  wire [7:0] source_pixel;
  sprite_rom u_sprite (
      .clk(clk), .address({source_address[9:6], source_address[4:1]}), .pixel(source_pixel)
  );
  // 512 uploaded 8x8 tiles, 2 bits/pixel. One 16-bit packed row per address.
  // Small, single-read tile cache uses LUT RAM to reserve BRAM for code/frame data.
  (* ram_style="distributed" *) reg [15:0] tile_ram [0:4095];
  reg [15:0] tile_row_pixels;
  reg [7:0] palette [0:31];
  reg [8:0] cfg_tile, active_tile;
  reg [2:0] cfg_palette, active_palette;
  reg cfg_flip_x, cfg_flip_y, active_flip_x, active_flip_y;
  // Optional NES composition: 0 legacy, 1 background, 2 OAM-ordered sprite.
  reg [1:0] cfg_nes_mode, active_nes_mode;
  reg cfg_behind_bg, active_behind_bg;
  reg [7:0] cfg_row_mask, active_row_mask;
  // Only the drawing bank needs metadata; full clear initializes every frame.
  (* ram_style="block" *) reg [1:0] nes_meta [0:FRAME_WIDTH*FRAME_HEIGHT-1];
  reg [1:0] nes_meta_read;
  reg signed [15:0] cfg_tile_x, cfg_tile_y, tile_x, tile_y;
  reg [11:0] cfg_upload_address, upload_address;
  reg [15:0] upload_data;
  reg upload_pending;
  reg [4:0] cfg_palette_address;
  reg tile_mode;
  reg [2:0] tile_column, tile_row;
  reg [31:0] clipped_count;
  reg [15:0] cfg_clip_x0, cfg_clip_y0, cfg_clip_x1, cfg_clip_y1;
  reg [15:0] active_clip_x0, active_clip_y0, active_clip_x1, active_clip_y1;
  reg [31:0] cpu_pixel_address, cycle_count, software_cycles, hardware_cycles;
  reg cpu_pixel_pending;
  // Pending is publicly busy immediately; execution starts one cycle later.
  reg tile_launch_pending;
  wire [10:0] source_end_x = {6'd0, cfg_source_x} + {1'b0, cfg_w};
  wire [9:0] source_end_y = {5'd0, cfg_source_y} + {1'b0, cfg_h};
  wire [10:0] end_x = {1'b0, cfg_x} + {1'b0, cfg_w};
  wire [ 9:0] end_y = {1'b0, cfg_y} + {1'b0, cfg_h};
  reg draw_launch_pending;
  reg [2:0] draw_launch_command;
  wire launch_pending = tile_launch_pending || draw_launch_pending;
  wire command_write = write_enable && write_strobe == 4'b1111 && address == 32'h1000_020c;
  wire command_idle = !busy && !launch_pending && !(swap_pending || native_blocked) && !cpu_pixel_pending;
  wire fill_command_write = command_write && (write_data == 1 || write_data == 3);
  wire fill_config_valid = !(cfg_w == 0 || cfg_h == 0 || cfg_y >= FRAME_HEIGHT ||
      end_x > FRAME_WIDTH || end_y > FRAME_HEIGHT ||
      (write_data == 3 && (source_end_x > 32 || source_end_y > 32)));
  wire blit_command_write = command_write && write_data == 6 && DDR_FRAME && DDR_BITMAP;
  wire blit_config_valid = !(cfg_bitmap_alpha > 0 && cfg_bitmap_alpha < 4 && !staging_initialized);
  wire draw_accept = command_idle && ((fill_command_write && fill_config_valid) ||
      (blit_command_write && blit_config_valid));
  wire fill_launch = REGISTER_DRAW_START ?
      (draw_launch_pending && (draw_launch_command == 1 || draw_launch_command == 3)) :
      (command_idle && fill_command_write && fill_config_valid);
  wire blit_launch = REGISTER_DRAW_START ? (draw_launch_pending && draw_launch_command == 6) :
      (command_idle && blit_command_write && blit_config_valid);
  wire [2:0] fill_command = REGISTER_DRAW_START ? draw_launch_command : write_data[2:0];
  always @(posedge clk) begin
    if (!reset_n) begin draw_launch_pending <= 0;draw_launch_command <= 0;end
    else begin
      draw_launch_pending <= REGISTER_DRAW_START && draw_accept;
      if (REGISTER_DRAW_START && draw_accept) draw_launch_command <= write_data[2:0];
    end
  end

  wire tile_write = write_enable && write_strobe == 4'b1111 &&
      address == 32'h1000_020c && write_data == 5;
  wire tile_accept = tile_write && !busy && !launch_pending && !(swap_pending || native_blocked) && !cpu_pixel_pending;
  wire tile_launch = REGISTER_TILE_START ? tile_launch_pending : tile_accept;
  always @(posedge clk) begin
    if (!reset_n) tile_launch_pending <= 0;
    else tile_launch_pending <= REGISTER_TILE_START && tile_accept;
  end
  wire native_begin=NATIVE800 && command_write && write_data==7 && !native_active &&
      !busy && !launch_pending && !cpu_pixel_pending && !scan_bus_busy && !blit_busy && !read_mux_busy && frame_enable;
  wire native_flush=NATIVE800 && command_write && (write_data==8 || write_data==9) && command_idle && staging_initialized;
  wire native_abort=NATIVE800 && command_write && write_data==10;
  assign frame_copies=NATIVE800 ? native_frames : band_copies;
  assign frame_fault=dma_fault || (NATIVE800 && native_failed);
  generate if(NATIVE800)begin:g_native
    native_frame_commit u_commit(.clk(clk),.reset_n(reset_n),.enable(frame_enable),.begin_frame(native_begin),
      .band_valid(native_flush),.band_id(write_data==9),.frame_edge(frame_edge),.abort_frame(native_abort),
      .copy_done(dma_done),.copy_busy(dma_busy),.bus_busy(scan_bus_busy||blit_busy||read_mux_busy),.copy_fault(dma_fault),
      .copy_start(native_copy_start),.copy_band(native_copy_band),.copy_target_bank(native_target),
      .front(native_front),.display_valid(native_valid),.need_band(native_need_band),.active(native_active),
      .ready_to_present(native_ready),.presented(native_presented),.failed(native_failed),.completed_frames(native_frames));
  end else begin:g_not_native
    assign native_copy_start=0;assign native_copy_band=0;assign native_target=0;assign native_front=0;
    assign native_valid=0;assign native_need_band=0;assign native_active=0;assign native_ready=0;
    assign native_presented=0;assign native_failed=0;assign native_frames=0;
  end endgenerate
  reg dma_started, dma_finished;
  wire dma_start = NATIVE800 ? native_copy_start : DDR_FRAME && frame_enable && swap_pending && !busy && !launch_pending && !cpu_pixel_pending && !dma_started;
  assign frame_presented = NATIVE800 ? native_presented : reset_n && frame_edge && swap_pending && !busy && !launch_pending && !cpu_pixel_pending &&
      (!DDR_FRAME || dma_finished);
  reg [18:0] cpu_write_address;
  reg [7:0] cpu_write_color;
  // 像素端口有一拍延后写；给快速CPU背压，其余寄存器仍保留原busy/忽略写规则。
  assign write_ready = address != 32'h1000_024c || !cpu_pixel_pending;
  integer palette_i;
  wire [2:0] source_tile_column = active_flip_x ? ~tile_column : tile_column;
  wire [2:0] source_tile_row = active_flip_y ? ~tile_row : tile_row;
  wire [15:0] selected_tile_bits = tile_row_pixels >> {source_tile_column, 1'b0};
  wire [1:0] tile_index = selected_tile_bits[1:0];
  wire signed [16:0] tile_dest_x = {tile_x[15],tile_x} + $signed({14'd0,tile_column});
  wire signed [16:0] tile_dest_y = {tile_y[15],tile_y} + $signed({14'd0,tile_row});
  wire clipped_pixel = tile_mode && (tile_dest_x < 0 || tile_dest_x >= FRAME_WIDTH ||
                                     tile_dest_y < 0 || tile_dest_y >= FRAME_HEIGHT ||
                                     tile_dest_x < $signed({1'b0,active_clip_x0}) ||
                                     tile_dest_x >= $signed({1'b0,active_clip_x1}) ||
                                     tile_dest_y < $signed({1'b0,active_clip_y0}) ||
                                     tile_dest_y >= $signed({1'b0,active_clip_y1}) ||
                                     !active_row_mask[tile_row]);
  wire consume_pixel = busy && !blit_mode && ((!copy_mode && !tile_mode) ||
      (tile_mode && DDR_FRAME && FAST_TILE_ROW ? !tile_row_wait : copy_phase));
  wire transparent_pixel = tile_mode ? tile_index == 0 :
      copy_mode && copy_transparent && source_pixel == copy_key;
  // The first nonzero OAM pixel owns the location even when hidden by BG.
  wire nes_hidden = tile_mode && active_nes_mode==2 &&
      (nes_meta_read[1] || (active_behind_bg && nes_meta_read[0]));
  wire write_pixel = consume_pixel && !transparent_pixel && !clipped_pixel && !nes_hidden;
  wire [7:0] write_color = tile_mode ? palette[{active_palette,tile_index}] :
      copy_mode ? source_pixel : fill_color;
  wire [18:0] tile_address = DDR_FRAME ? ((NATIVE800 ? (({10'd0,tile_dest_y[8:0]}<<9)+({10'd0,tile_dest_y[8:0]}<<8)+({10'd0,tile_dest_y[8:0]}<<5)) : (({10'd0,tile_dest_y[8:0]}<<9)+({10'd0,tile_dest_y[8:0]}<<7)))+{9'd0,tile_dest_x[9:0]}) :
      COMPACT_FRAME ? {2'd0,tile_dest_y[7:0],tile_dest_x[7:0]} :
      {tile_dest_y[8:0],tile_dest_x[8:0]};
  wire [18:0] write_address = tile_mode ? tile_address : fill_address;
  wire [18:0] staging_address = blit_pixel_valid ? blit_pixel_address : cpu_pixel_pending ? cpu_write_address : write_address;
  wire [7:0] staging_color = blit_pixel_valid ? blit_pixel_color : cpu_pixel_pending ? cpu_write_color : write_color;
  wire [2:0] fill_step = DDR_FRAME && !copy_mode && !tile_mode &&
      fill_address[1:0]==0 && remaining_x>=4 ? 3'd4 : 3'd1;
  wire [3:0] staging_strobe = !blit_mode && !cpu_pixel_pending && fill_step==4 ? 4'b1111 :
      (4'b0001 << staging_address[1:0]);
  wire nes_meta_write = !DDR_FRAME && (cpu_pixel_pending || (consume_pixel && !clipped_pixel &&
      (!tile_mode || active_nes_mode==1 ||
       (active_nes_mode==2 && tile_index!=0 && !nes_meta_read[1]))));
  wire [1:0] nes_meta_data = !tile_mode || cpu_pixel_pending ? 2'b00 :
      active_nes_mode==1 ? {1'b0,tile_index!=0} : {1'b1,nes_meta_read[0]};
  wire [10:0] view_x = scan_x - 11'd256;
  wire [9:0] view_y = scan_y - 10'd60;
  wire scan_inside = COMPACT_FRAME ?
      (scan_x >= 256 && scan_x < 768 && scan_y >= 60 && scan_y < 540) :
      (scan_x < 1024 && scan_y < 600);
  wire [18:0] scan_address = !scan_inside ? 18'd0 : COMPACT_FRAME ?
      {2'd0,view_y[8:1],view_x[8:1]} : {scan_y[9:1],scan_x[9:1]};
  wire [ 7:0] pixel = front_d ? read1 : read0;
  wire [23:0] ddr_rgb;
  wire [18:0] dma_address;
  reg [31:0] dma_word0,dma_word1,dma_word2a,dma_word2b,dma_word2c;
  reg [1:0] dma_tail_bank;
  wire [31:0] dma_word2=dma_tail_bank==0 ? dma_word2a : dma_tail_bank==1 ? dma_word2b : dma_word2c;
  reg [1:0] dma_bank;
  // 位图绘制与整帧复制互斥；Alpha复用原同步BRAM读端口，无新增帧存。
  wire [18:0] ram_read_address=blit_busy ? blit_pixel_address : dma_address;
  wire [31:0] dma_data = dma_bank==0 ? dma_word0 : dma_bank==1 ? dma_word1 : dma_word2;
  wire [7:0] blit_destination_color=dma_data >> {blit_pixel_address[1:0],3'b000};
  assign scan_rgb = DDR_FRAME ? ddr_rgb : valid_d ? {pixel[7:5],pixel[7:5],pixel[7:6],
        pixel[4:2],pixel[4:2],pixel[4:3],pixel[1:0],pixel[1:0],pixel[1:0],pixel[1:0]} : 24'h203040;
  generate
    if (DDR_FRAME) begin : g_ddr_frame
      ddr_framebuffer #(.NATIVE800(NATIVE800)) u_ddr_frame (
        .clk(clk),.reset_n(reset_n),.enable(frame_enable),.base(frame_base),.front(front),
        .display_valid(NATIVE800 ? native_valid : (front ? valid1 : valid0)),.copy_start(dma_start),
        .copy_band(native_copy_band),.copy_bank(native_target),.copy_cancel(native_abort||native_failed),.copy_done(dma_done),.copy_busy(dma_busy),
        .source_address(dma_address),.source_data(dma_data),.scan_de(scan_de),.scan_x(scan_x),.scan_y(scan_y),
        .scan_rgb(ddr_rgb),.bus_busy(scan_bus_busy),.fault(dma_fault),.underflows(frame_underflows),.write_frames(band_copies),
        .araddr(scan_araddr),.arlen(scan_arlen),.arvalid(scan_arvalid),.arready(scan_arready),
        .rdata(frame_rdata),.rresp(frame_rresp),.rlast(frame_rlast),.rvalid(scan_rvalid),.rready(scan_rready),
        .awaddr(frame_awaddr),.awlen(frame_awlen),.awvalid(frame_awvalid),.awready(frame_awready),
        .wdata(frame_wdata),.wstrb(frame_wstrb),.wlast(frame_wlast),.wvalid(frame_wvalid),.wready(frame_wready),
        .bresp(frame_bresp),.bvalid(frame_bvalid),.bready(frame_bready)
      );
    end else begin : g_bram_frame
      assign ddr_rgb=0;assign dma_address=0;assign dma_done=0;assign dma_busy=0;
      assign scan_bus_busy=0;assign dma_fault=0;assign frame_underflows=0;assign band_copies=0;
      assign scan_araddr=0;assign scan_arlen=0;assign scan_arvalid=0;assign scan_rready=0;
      assign frame_awaddr=0;assign frame_awlen=0;assign frame_awvalid=0;
      assign frame_wdata=0;assign frame_wstrb=0;assign frame_wlast=0;assign frame_wvalid=0;assign frame_bready=0;
    end
  endgenerate

  generate
    if(DDR_FRAME && DDR_BITMAP) begin : g_bitmap
      ddr_bitmap_blit #(.FRAME_WIDTH(FRAME_WIDTH),.FRAME_HEIGHT(FRAME_HEIGHT)) u_blit(
        .clk(clk),.reset_n(reset_n),.enable(frame_enable && asset_enable),.start(blit_start),
        .asset_base(asset_base),.asset_size(asset_size),.source_offset(cfg_bitmap_offset),
        .stride(cfg_bitmap_stride),.width(cfg_w),.height(cfg_h),.dest_x(cfg_tile_x),.dest_y(cfg_tile_y),
        .flip_x(cfg_flip_x),.flip_y(cfg_flip_y),.transparent(cfg_transparent),.key(cfg_key),
        .opacity(cfg_bitmap_alpha),.destination_color(blit_destination_color),
        .clip_x0(cfg_clip_x0),.clip_y0(cfg_clip_y0),.clip_x1(cfg_clip_x1),.clip_y1(cfg_clip_y1),
        .busy(blit_busy),.done(blit_done),.fault(blit_fault),.pixel_valid(blit_pixel_valid),
        .pixel_address(blit_pixel_address),.pixel_color(blit_pixel_color),
        .written(blit_written),.skipped(blit_skipped),.clipped(blit_clipped),
        .araddr(blit_araddr),.arlen(blit_arlen),.arvalid(blit_arvalid),.arready(blit_arready),
        .rdata(frame_rdata),.rresp(frame_rresp),.rlast(frame_rlast),.rvalid(blit_rvalid),.rready(blit_rready));
      ddr_read_mux u_read_mux(
        .scan_reserve(scan_bus_busy),
        .clk(clk),.reset_n(reset_n),.scan_araddr(scan_araddr),.scan_arlen(scan_arlen),
        .scan_arvalid(scan_arvalid),.scan_arready(scan_arready),.scan_rvalid(scan_rvalid),.scan_rready(scan_rready),
        .blit_araddr(blit_araddr),.blit_arlen(blit_arlen),.blit_arvalid(blit_arvalid),.blit_arready(blit_arready),
        .blit_rvalid(blit_rvalid),.blit_rready(blit_rready),
        .araddr(frame_araddr),.arlen(frame_arlen),.arvalid(frame_arvalid),.arready(frame_arready),
        .rvalid(frame_rvalid),.rlast(frame_rlast),.rready(frame_rready),.busy(read_mux_busy));
    end else begin : g_no_bitmap
      assign blit_done=0;assign blit_busy=0;assign blit_fault=0;assign blit_pixel_valid=0;
      assign blit_pixel_address=0;assign blit_pixel_color=0;
      assign blit_written=0;assign blit_skipped=0;assign blit_clipped=0;assign read_mux_busy=0;
      assign frame_araddr=scan_araddr;assign frame_arlen=scan_arlen;assign frame_arvalid=scan_arvalid;
      assign scan_arready=frame_arready;assign scan_rvalid=frame_rvalid;assign frame_rready=scan_rready;
    end
  endgenerate

  // RAM contents/output registers are not reset: BRAM inference is preserved.
  // Display is masked until a full clear initializes the selected bank.
  always @(posedge clk) begin
    tile_row_pixels <= tile_ram[{active_tile,source_tile_row}];
    if (upload_pending) tile_ram[upload_address] <= upload_data;
    if (!DDR_FRAME) nes_meta_read <= nes_meta[write_address];
    else nes_meta_read <= 0;
    if (nes_meta_write)
      nes_meta[cpu_pixel_pending ? cpu_write_address : write_address] <= nes_meta_data;
    // Always-enabled reads keep asynchronous timing DE off RAM enables.
    if (DDR_FRAME) begin
      dma_word0 <= dma_words0[ram_read_address[16:2]];
      dma_word1 <= dma_words1[ram_read_address[16:2]];
      dma_word2a <= NATIVE800 ? 32'd0 : dma_words2a[ram_read_address[14:2]];
      dma_word2b <= NATIVE800 ? 32'd0 : dma_words2b[ram_read_address[12:2]];
      dma_word2c <= NATIVE800 ? 32'd0 : dma_words2c[ram_read_address[11:2]];
      dma_tail_bank <= !ram_read_address[15] ? 2'd0 : !ram_read_address[13] ? 2'd1 : 2'd2;
      dma_bank <= ram_read_address[18:17];
    end
    else begin read0 <= bank0[scan_address];read1 <= bank1[scan_address];end
    // busy is synchronously reset. RAM writes must not be gated directly
    // by the external reset synchronizer's asynchronously asserted output.
    if (write_pixel || cpu_pixel_pending || blit_pixel_valid) begin
      if (DDR_FRAME) begin
        if (staging_address[18:17]==2'd0) begin
          if (staging_strobe[0]) dma_words0[staging_address[16:2]][7:0] <= staging_color;
          if (staging_strobe[1]) dma_words0[staging_address[16:2]][15:8] <= staging_color;
          if (staging_strobe[2]) dma_words0[staging_address[16:2]][23:16] <= staging_color;
          if (staging_strobe[3]) dma_words0[staging_address[16:2]][31:24] <= staging_color;
        end
        if (staging_address[18:17]==2'd1) begin
          if (staging_strobe[0]) dma_words1[staging_address[16:2]][7:0] <= staging_color;
          if (staging_strobe[1]) dma_words1[staging_address[16:2]][15:8] <= staging_color;
          if (staging_strobe[2]) dma_words1[staging_address[16:2]][23:16] <= staging_color;
          if (staging_strobe[3]) dma_words1[staging_address[16:2]][31:24] <= staging_color;
        end
        if (!NATIVE800 && staging_address[18:17]==2'd2 && !staging_address[15]) begin
          if (staging_strobe[0]) dma_words2a[staging_address[14:2]][7:0] <= staging_color;
          if (staging_strobe[1]) dma_words2a[staging_address[14:2]][15:8] <= staging_color;
          if (staging_strobe[2]) dma_words2a[staging_address[14:2]][23:16] <= staging_color;
          if (staging_strobe[3]) dma_words2a[staging_address[14:2]][31:24] <= staging_color;
        end
        if (!NATIVE800 && staging_address[18:17]==2'd2 && staging_address[15:13]==3'b100) begin
          if (staging_strobe[0]) dma_words2b[staging_address[12:2]][7:0] <= staging_color;
          if (staging_strobe[1]) dma_words2b[staging_address[12:2]][15:8] <= staging_color;
          if (staging_strobe[2]) dma_words2b[staging_address[12:2]][23:16] <= staging_color;
          if (staging_strobe[3]) dma_words2b[staging_address[12:2]][31:24] <= staging_color;
        end
        if (!NATIVE800 && staging_address[18:17]==2'd2 && staging_address[15:12]==4'b1010) begin
          if (staging_strobe[0]) dma_words2c[staging_address[11:2]][7:0] <= staging_color;
          if (staging_strobe[1]) dma_words2c[staging_address[11:2]][15:8] <= staging_color;
          if (staging_strobe[2]) dma_words2c[staging_address[11:2]][23:16] <= staging_color;
          if (staging_strobe[3]) dma_words2c[staging_address[11:2]][31:24] <= staging_color;
        end
      end else if (front) bank0[cpu_pixel_pending ? cpu_write_address : write_address] <= cpu_pixel_pending ? cpu_write_color : write_color;
      else bank1[cpu_pixel_pending ? cpu_write_address : write_address] <= cpu_pixel_pending ? cpu_write_color : write_color;
    end
  end
  always @(*) begin
    case (address)
      32'h1000_0200: read_data = {7'd0, cfg_y, 6'd0, cfg_x};
      32'h1000_0204: read_data = {7'd0, cfg_h, 6'd0, cfg_w};
      32'h1000_0208: read_data = {24'd0, cfg_color};
      32'h1000_0210: read_data = {28'd0, error, (front ? valid1 : valid0), swap_pending, (busy || launch_pending || cpu_pixel_pending || native_blocked)};
      32'h1000_0214: read_data = pixel_count;
      32'h1000_0218: read_data = command_count;
      32'h1000_021c: read_data = swap_count;
      32'h1000_0220: read_data = {11'd0, cfg_source_y, 11'd0, cfg_source_x};
      32'h1000_0224: read_data = {16'd0, cfg_key, 7'd0, cfg_transparent};
      32'h1000_0228: read_data = skipped_count;
      32'h1000_022c: read_data = {3'd0,cfg_behind_bg,cfg_nes_mode,cfg_flip_y,cfg_flip_x,5'd0,cfg_palette,7'd0,cfg_tile};
      32'h1000_0230: read_data = {20'd0,cfg_upload_address};
      32'h1000_0234: read_data = {27'd0,cfg_palette_address};
      32'h1000_023c: read_data = {24'd0,palette[cfg_palette_address]};
      32'h1000_0240: read_data = {cfg_tile_y,cfg_tile_x};
      32'h1000_0244: read_data = clipped_count;
      32'h1000_0248: read_data = cpu_pixel_address;
      32'h1000_0250: read_data = cycle_count;
      32'h1000_0254: read_data = software_cycles;
      32'h1000_0258: read_data = hardware_cycles;
      32'h1000_025c: read_data = {cfg_clip_y0,cfg_clip_x0};
      32'h1000_0260: read_data = {cfg_clip_y1,cfg_clip_x1};
      32'h1000_0264: read_data = (FRAME_HEIGHT << 16) | FRAME_WIDTH;
      32'h1000_0280: read_data = NATIVE800 ? ((600<<16)|800) : 0;
      32'h1000_0284: read_data = NATIVE800 ? {27'd0,native_failed,native_ready,native_active,native_need_band,native_copy_band} : 0;
      32'h1000_0268: read_data = {24'd0,cfg_row_mask};
      32'h1000_0270: read_data = cfg_bitmap_offset;
      32'h1000_0274: read_data = {16'd0,cfg_bitmap_stride};
      32'h1000_0278: read_data = {28'd0,DDR_BITMAP!=0,DDR_BITMAP!=0,blit_fault,blit_busy};
      32'h1000_027c: read_data = {29'd0,cfg_bitmap_alpha};
      default: read_data = 0;
    endcase
  end
  always @(posedge clk) begin
    if (!reset_n) begin
      front <= 0;
      front_d <= 0;
      valid_d <= 0;
      valid0 <= 0;
      valid1 <= 0;
      busy <= 0;blit_mode <= 0;blit_start <= 0;cfg_bitmap_offset <= 0;cfg_bitmap_stride <= 0;cfg_bitmap_alpha <= 4;
      dma_started <= 0;
      dma_finished <= 0;
      swap_pending <= 0;
      error <= 0;
      cfg_x <= 0;
      cfg_y <= 0;
      cfg_w <= 0;
      cfg_h <= 0;
      cfg_color <= 0;
      fill_color <= 0;
      fill_address <= 0;
      row_address <= 0;
      fill_width <= 0;
      remaining_x <= 0;
      remaining_y <= 0;
      full_clear <= 0;staging_initialized <= 0;
      pixel_count <= 0;
      command_count <= 0;
      swap_count <= 0;
      cfg_source_x <= 0;
      cfg_source_y <= 0;
      cfg_transparent <= 0;
      cfg_key <= 0;
      copy_key <= 0;
      copy_mode <= 0;
      copy_phase <= 0;tile_row_wait <= 0;
      copy_transparent <= 0;
      source_address <= 0;
      source_row_address <= 0;
      skipped_count <= 0;
      cfg_tile <= 0;
      active_tile <= 0;
      cfg_palette <= 0;
      active_palette <= 0;
      cfg_flip_x <= 0;
      cfg_flip_y <= 0;
      active_flip_x <= 0;
      active_flip_y <= 0;
      cfg_nes_mode <= 0;
      active_nes_mode <= 0;
      cfg_behind_bg <= 0;
      active_behind_bg <= 0;
      cfg_row_mask <= 8'hff;
      active_row_mask <= 8'hff;
      cfg_tile_x <= 0;
      cfg_tile_y <= 0;
      tile_x <= 0;
      tile_y <= 0;
      cfg_upload_address <= 0;
      upload_address <= 0;
      upload_data <= 0;
      upload_pending <= 0;
      cfg_palette_address <= 0;
      tile_mode <= 0;
      tile_column <= 0;
      tile_row <= 0;
      clipped_count <= 0;
      cfg_clip_x0 <= 0;
      cfg_clip_y0 <= 0;
      cfg_clip_x1 <= FRAME_WIDTH;
      cfg_clip_y1 <= FRAME_HEIGHT;
      active_clip_x0 <= 0;
      active_clip_y0 <= 0;
      active_clip_x1 <= FRAME_WIDTH;
      active_clip_y1 <= FRAME_HEIGHT;
      cpu_pixel_address <= 0;
      cpu_pixel_pending <= 0;
      cpu_write_address <= 0;
      cpu_write_color <= 0;
      cycle_count <= 0;
      software_cycles <= 0;
      hardware_cycles <= 0;
      for (palette_i=0; palette_i<32; palette_i=palette_i+1) palette[palette_i] <= 0;
    end else begin
      blit_start <= 0;
      if(blit_done && blit_mode) begin
        busy <= 0;blit_mode <= 0;
        pixel_count <= pixel_count+blit_written;
        skipped_count <= skipped_count+blit_skipped;
        clipped_count <= clipped_count+blit_clipped;
        if(blit_fault) error <= 1;
        else command_count <= command_count+1'b1;
      end
      if (dma_start) dma_started <= 1;
      if (dma_done) dma_finished <= 1;
      if(NATIVE800 && (native_begin || dma_done))staging_initialized<=0;
      if (DDR_FRAME && frame_fault) error <= 1;
      upload_pending <= 0;
      cpu_pixel_pending <= 0;
      cycle_count <= cycle_count + 1'b1;
      if (cpu_pixel_pending) pixel_count <= pixel_count + 1'b1;
      front_d <= front;
      valid_d <= scan_de && scan_inside && (front ? valid1 : valid0);
      // COPY alternates synchronous ROM read and consume/write cycles.
      if (busy && (copy_mode || (tile_mode && !(DDR_FRAME && FAST_TILE_ROW)))) copy_phase <= !copy_phase;
      if (busy && tile_mode && DDR_FRAME && FAST_TILE_ROW && tile_row_wait) tile_row_wait <= 0;
      if (consume_pixel) begin
        if (write_pixel) pixel_count <= pixel_count + fill_step;
        else if (clipped_pixel) clipped_count <= clipped_count + 1'b1;
        else skipped_count <= skipped_count + 1'b1;
        if (tile_mode) begin
          tile_column <= tile_column + 1'b1;
          if (tile_column == 7) begin
            tile_row <= tile_row + 1'b1;
            if(DDR_FRAME && FAST_TILE_ROW) tile_row_wait <= 1;
          end
        end
        if (remaining_x == fill_step) begin
          if (remaining_y == 1) begin
            busy <= 0;
            command_count <= command_count + 1'b1;
            if (full_clear) begin
              staging_initialized <= 1;
              if (front) valid0 <= 1;
              else valid1 <= 1;
            end
          end else begin
            remaining_y  <= remaining_y - 1'b1;
            remaining_x  <= fill_width;
            row_address  <= row_address + FRAME_WIDTH;
            fill_address <= row_address + FRAME_WIDTH;
            source_row_address <= source_row_address + 10'd32;
            source_address <= source_row_address + 10'd32;
          end
        end else begin
          remaining_x  <= remaining_x - fill_step;
          fill_address <= fill_address + fill_step;
          source_address <= source_address + 1'b1;
        end
      end
      if (frame_presented) begin
        front <= !front;
        swap_pending <= 0;
        swap_count <= swap_count + 1'b1;
        dma_started <= 0;
        dma_finished <= 0;
      end
      if (fill_launch) begin
        busy <= 1;
        fill_color <= cfg_color;
        fill_address <= DDR_FRAME ? ((NATIVE800 ? (({10'd0,cfg_y}<<9)+({10'd0,cfg_y}<<8)+({10'd0,cfg_y}<<5)) : (({10'd0,cfg_y}<<9)+({10'd0,cfg_y}<<7)))+{9'd0,cfg_x}) : COMPACT_FRAME ? {2'd0,cfg_y[7:0],cfg_x[7:0]} : {cfg_y,cfg_x};
        row_address <= DDR_FRAME ? ((NATIVE800 ? (({10'd0,cfg_y}<<9)+({10'd0,cfg_y}<<8)+({10'd0,cfg_y}<<5)) : (({10'd0,cfg_y}<<9)+({10'd0,cfg_y}<<7)))+{9'd0,cfg_x}) : COMPACT_FRAME ? {2'd0,cfg_y[7:0],cfg_x[7:0]} : {cfg_y,cfg_x};
        fill_width <= cfg_w;
        remaining_x <= cfg_w;
        remaining_y <= cfg_h;
        full_clear <= fill_command == 1 && cfg_x == 0 && cfg_y == 0 && cfg_w == FRAME_WIDTH && cfg_h == FRAME_HEIGHT;
        copy_mode <= fill_command == 3;
        tile_mode <= 0;
        copy_phase <= 0;
        copy_transparent <= cfg_transparent;
        copy_key <= cfg_key;
        source_address <= {cfg_source_y, cfg_source_x};
        source_row_address <= {cfg_source_y, cfg_source_x};
      end
      if (blit_launch) begin
        busy <= 1;blit_mode <= 1;blit_start <= 1;
        copy_mode <= 0;tile_mode <= 0;full_clear <= 0;
      end
      if (tile_launch) begin
        // TILE always visits 64 candidates; signed destination is clipped.
        busy <= 1;
        copy_mode <= 0;
        tile_mode <= 1;
        copy_phase <= 0;tile_row_wait <= DDR_FRAME && FAST_TILE_ROW;
        full_clear <= 0;
        fill_width <= 8;
        remaining_x <= 8;
        remaining_y <= 8;
        tile_column <= 0;
        tile_row <= 0;
        active_tile <= cfg_tile;
        active_palette <= cfg_palette;
        active_flip_x <= cfg_flip_x;
        active_flip_y <= cfg_flip_y;
        active_nes_mode <= cfg_nes_mode;
        active_behind_bg <= cfg_behind_bg;
        active_row_mask <= cfg_row_mask;
        tile_x <= cfg_tile_x;
        tile_y <= cfg_tile_y;
        active_clip_x0 <= cfg_clip_x0;
        active_clip_y0 <= cfg_clip_y0;
        active_clip_x1 <= cfg_clip_x1;
        active_clip_y1 <= cfg_clip_y1;
      end
      if (write_enable && write_strobe == 4'b1111) begin
        if (address == 32'h1000_020c) begin
          if(NATIVE800 && write_data==7)begin if(!native_begin || native_failed || dma_fault)error<=1;end
          else if(NATIVE800 && write_data==10)begin end
          else if (write_data == 4) error <= 0;
          else if (busy || launch_pending || swap_pending || cpu_pixel_pending || native_blocked) error <= 1;
          else if(NATIVE800 && (write_data==8 || write_data==9))begin if(!native_flush)error<=1;end
          else if (write_data == 1 || write_data == 3) begin
            if (cfg_w == 0 || cfg_h == 0 || cfg_y >= FRAME_HEIGHT || end_x > FRAME_WIDTH || end_y > FRAME_HEIGHT ||
                (write_data == 3 && (source_end_x > 32 || source_end_y > 32))) error <= 1;
            else begin
              // fill_launch owns valid command initialization above.
            end
          end else if (write_data == 5) begin
            // tile_launch owns the initialization above.
          end else if (write_data == 6 && DDR_FRAME && DDR_BITMAP) begin
            if(cfg_bitmap_alpha>0 && cfg_bitmap_alpha<4 && !staging_initialized) error <= 1;
            else begin
              // blit_launch owns valid command initialization above.
            end
          end else if (write_data == 2) begin
            if(NATIVE800)error<=1;
            else if (front ? valid0 : valid1) swap_pending <= 1;
            else error <= 1;
          end else error <= 1;
        end else if (!busy && !launch_pending && !(swap_pending || native_blocked)) begin
          case (address)
            32'h1000_0200: begin
              cfg_x <= write_data[9:0];
              cfg_y <= write_data[24:16];
            end
            32'h1000_0204: begin
              cfg_w <= write_data[9:0];
              if(NATIVE800 && write_data[31:25]!=0)begin cfg_h<=0;error<=1;end
              else cfg_h <= write_data[24:16];
            end
            32'h1000_0208: cfg_color <= write_data[7:0];
            32'h1000_0220: begin
              cfg_source_x <= write_data[4:0];
              cfg_source_y <= write_data[20:16];
            end
            32'h1000_0224: begin
              cfg_transparent <= write_data[0];
              cfg_key <= write_data[15:8];
            end
            32'h1000_022c: begin
              cfg_tile <= write_data[8:0];
              cfg_palette <= write_data[18:16];
              cfg_flip_x <= write_data[24];
              cfg_flip_y <= write_data[25];
              cfg_nes_mode <= write_data[27:26];
              cfg_behind_bg <= write_data[28];
            end
            32'h1000_0230: cfg_upload_address <= write_data[11:0];
            32'h1000_0234: cfg_palette_address <= write_data[4:0];
            32'h1000_0238: begin
              upload_address <= cfg_upload_address;
              upload_data <= write_data[15:0];
              upload_pending <= 1;
              cfg_upload_address <= cfg_upload_address + 1'b1;
            end
            32'h1000_023c: palette[cfg_palette_address] <= write_data[7:0];
            32'h1000_0240: begin
              cfg_tile_x <= write_data[15:0];
              cfg_tile_y <= write_data[31:16];
            end
            32'h1000_0248: cpu_pixel_address <= write_data;
            32'h1000_024c: begin
              if (cpu_pixel_pending || cpu_pixel_address >= FRAME_WIDTH*FRAME_HEIGHT) error <= 1;
              else begin
                cpu_write_address <= cpu_pixel_address[18:0];
                cpu_write_color <= write_data[7:0];
                cpu_pixel_pending <= 1;
                cpu_pixel_address <= cpu_pixel_address + 1'b1;
              end
            end
            32'h1000_0254: software_cycles <= write_data;
            32'h1000_0258: hardware_cycles <= write_data;
            // TILE-only half-open viewport; intersect with physical frame bounds.
            // Empty/inverted rectangles clip all candidates; FILL/COPY are unchanged.
            32'h1000_025c: begin
              cfg_clip_x0 <= write_data[15:0];
              cfg_clip_y0 <= write_data[31:16];
            end
            32'h1000_0260: begin
              cfg_clip_x1 <= write_data[15:0];
              cfg_clip_y1 <= write_data[31:16];
            end
            32'h1000_0268: cfg_row_mask <= write_data[7:0];
            32'h1000_0270: cfg_bitmap_offset <= write_data;
            32'h1000_0274: cfg_bitmap_stride <= write_data[15:0];
            32'h1000_027c: cfg_bitmap_alpha <= write_data[2:0];
            default: begin
            end
          endcase
        end else if (address == 32'h1000_0238 || address == 32'h1000_023c || address == 32'h1000_024c) error <= 1;
      end
      if (write_enable && write_strobe != 4'b1111 && address == 32'h1000_024c) error <= 1;
    end
  end
endmodule
