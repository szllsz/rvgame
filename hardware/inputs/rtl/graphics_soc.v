`timescale 1ns / 1ps
module graphics_soc #(
    parameter MACHINE_IRQ_ENABLE = 0,
    parameter CPU_DUAL_ALU = 1,
    parameter MACHINE_TIMER_ENABLE = 0,
    parameter INDEPENDENT_CLOCKS = 0,
    parameter USE_C_PROGRAM = 0,  // 0保留板测汇编演示；1选择C运行环境自检
    parameter COMPACT_FRAME = 0,  // 1使用256x240游戏帧存；旧汇编使用0
    parameter DDR_BITMAP = 0, // 程序DDR区RGB332素材搬运
    parameter DDR_TILE_ROW = 0, // DDR TILE按行预读；正式旧版本默认关闭
    parameter NATIVE800 = 0,
    parameter DDR_FRAME = 0, // 1启用480x270 DDR显示试验；默认保留旧帧存
    parameter AUDIO_ENABLE = 0, // 自研CPU驱动APU和I2S；旧配置默认关闭
    parameter HOST_LOADER = 0, // 1启用PS程序加载，旧仿真默认关闭
    parameter GAMEPAD_INPUT = 0, // 旧仿真与汇编保持板载三键接口
    parameter SHOW_FPS = 0, // 1在游戏区域外显示实际换帧FPS
    parameter DDR_PROGRAM = 0, // 1为0x80000000窗口增加DDR只读缓存，0保留旧仿真
    parameter DDR_CACHE_LINES = 64,
    parameter DDR_CACHE_WAYS = 1,
    parameter DDR_FETCH_OVERLAP = 0,
    parameter DDR_CONTROL_OVERLAP = 0,
    parameter DDR_MEMORY_OVERLAP = 0,
    parameter DDR_LOCAL_LOAD_OVERLAP = 0,
    parameter DDR_DECODE_CONTROL = 0,
    parameter DDR_EARLY_HIT = 0,
    parameter DDR_FETCH_FOLD = 0,
    parameter DDR_DECODE_RAM_LOAD = 0,
    parameter DDR_SYNC_RESET = 0,
    parameter DDR_DECODE_DDR_LOAD = 0,
    parameter DDR_FAST_STORE = 0,
    parameter DDR_STORE_DIRECT_ADDR = 0,
    parameter DDR_LOAD_HANDOFF = 0,
    parameter DDR_HANDOFF_HINT = 0,
    parameter DDR_INSTRUCTION_BUFFER_WORDS = 0, // 0关闭；小型直接映射只读指令缓冲，flush/hold清除
    parameter DDR_FAST_DECODE = 0,
    parameter DDR_DECODE_STORE = 0,
    parameter DDR_FALLTHROUGH_FETCH = 0,
    parameter DDR_SERIAL_REGFILE = 0,
    parameter DDR_REGISTER_IMMEDIATE = 0,
    parameter DDR_SPLIT_INSTRUCTION_BOUNDS = 0,
    parameter DDR_FAST_ADDRESS_REGIONS = 0,
    parameter DDR_REGISTER_CONTROL_FETCH = 0,
    parameter AUDIO_SERIAL_FRAME_MEAN = 0,
    parameter SYSTEM_CLOCK_HZ = 50250000,
    parameter PERIPHERAL_CLOCK_HZ = SYSTEM_CLOCK_HZ,
    parameter GPU_REGISTER_DRAW_START = 0,
    parameter GPU_REGISTER_TILE_START = 0,
    parameter DDR_DECOUPLED_READ_HINT = 0,
    parameter DDR_ALIGNED_PAIR_SUPPLY = CPU_DUAL_ALU, // Optional neighbor word; CPU consumption is a separate stage.
    parameter DDR_INSTRUCTION_LINE_FILL = 0 // 1在完整成功的AXI指令行结束时发布16字，默认逐字填充
) (
    input wire clk,
    input wire machine_irq_external,machine_irq_software,machine_irq_timer, // CPU, working RAM, cache, loader and HP0 domain.
    input wire video_clk,input wire video_reset_n,input wire common_reset_n,
    input wire video_cpu_hold,input wire [31:0] video_asset_base,video_asset_size,
    input wire video_asset_enable,output wire cpu_quiescent,
    input wire reset_n,
    input wire cpu_hold,
    input wire load_write,
    input wire [31:0] load_address,
    input wire [31:0] load_data,
    input wire [7:0] pcm_host_address,input wire pcm_host_write,
    input wire [31:0] pcm_host_data,output wire pcm_host_ready,
    output wire [31:0] pcm_host_read_data,
    output wire [31:0] load_read_data,
    input wire [2:0] buttons_pressed,
    input wire [11:0] gamepad_buttons,
    input wire gamepad_connected,
    input wire [31:0] gamepad_left,gamepad_right,gamepad_aux,gamepad_sequence,
    input wire hs_in,
    input wire vs_in,
    input wire de_in,
    output reg hs,
    output reg vs,
    output reg de,
    output reg [23:0] rgb,
    output wire [3:0] led,
    output wire [31:0] ps_frame_count,
    output wire [31:0] ps_cpu_pc,
    output wire [31:0] ps_game_position,
    output wire [31:0] ps_present_count,ps_fps_frames,ps_fps_id,ps_fps_status,
    input wire [31:0] ddr_base,
    input wire [31:0] ddr_size,
    input wire ddr_enable,
    input wire ddr_flush,
    output wire ddr_busy,
    output reg ddr_fault,
    output wire [31:0] ddr_araddr,
    output wire [7:0] ddr_arlen,
    output wire [2:0] ddr_arsize,
    output wire [1:0] ddr_arburst,
    output wire [3:0] ddr_arcache,
    output wire [2:0] ddr_arprot,
    output wire ddr_arvalid,
    input wire ddr_arready,
    input wire [31:0] ddr_rdata,
    input wire [1:0] ddr_rresp,
    input wire ddr_rlast,
    input wire ddr_rvalid,
    output wire ddr_rready,
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
    output wire frame_bready,
    output wire pair_word_valid,output wire [31:0] pair_word_data,
    output wire audio_din,audio_bclk,audio_lrc
);
  wire [31:0] instruction_address, instruction_data;
  wire [31:0] instruction_cached_prediction;
  wire [31:0] instruction_resident_word,instruction_resident_prediction;
  wire instruction_prediction_metadata_ready;
  wire [31:0] pipeline_permission_address;
  wire pipeline_instruction_fault;
  wire [31:0] cpu_permission_address;
  wire cpu_permission_write;
  // Compare the24-bit window offset with a32-bit limit in parallel nibbles.
  // Keep the full upper limit: host sizes above16MiB must not wrap/truncate.
  function permission_offset_le;
    input [23:0] offset;
    input [31:0] limit;
    reg [5:0] less,equal;
    integer n;
    begin
      for(n=0;n<6;n=n+1) begin
        less[n]=offset[n*4+:4]<limit[n*4+:4];
        equal[n]=offset[n*4+:4]==limit[n*4+:4];
      end
      permission_offset_le=(|limit[31:24]) || less[5] ||
          (equal[5] && less[4]) || ((&equal[5:4]) && less[3]) ||
          ((&equal[5:3]) && less[2]) || ((&equal[5:2]) && less[1]) ||
          ((&equal[5:1]) && less[0]) || (&equal);
    end
  endfunction
  wire cpu_memory_permitted=
      (cpu_permission_address[31:17]==15'h1000) ||
      (cpu_permission_address[31:16]==16'h1000) ||
      (!cpu_permission_write && cpu_permission_address<32'h01000) ||
      (!cpu_permission_write && cpu_permission_address>=32'h30000000 && cpu_permission_address<32'h3000c000) ||
      (!cpu_permission_write && DDR_PROGRAM && cpu_permission_address[31:24]==8'h80 &&
        ddr_enable && ddr_size>=4 && permission_offset_le(cpu_permission_address[23:0],ddr_size-32'd4));
  wire [31:0] local_instruction_data;
  wire [31:0] data_address, data_write_data;
  wire instruction_request,data_read_request;
  wire framebuffer_write_ready;
  wire audio_write_ready;
  wire [3:0] data_write_strobe,cpu_write_mask;wire cpu_write_intent;
  wire peripheral_clk=INDEPENDENT_CLOCKS ? video_clk : clk;
  wire peripheral_reset_n=INDEPENDENT_CLOCKS ? video_reset_n : reset_n;
  wire peripheral_hold=INDEPENDENT_CLOCKS ? video_cpu_hold : cpu_hold;
  wire [31:0] peripheral_address,peripheral_write_data,mmio_reply;
  wire [3:0] peripheral_write_strobe;wire peripheral_write_enable;
  reg [31:0] peripheral_read_data;
  wire mmio_read_ready,mmio_write_ready,mmio_idle,ram_store_pending;
  wire data_write_enable;
  // CPU-local strongly ordered registers: mtime low/high, mtimecmp low/high, tick Hz.
  wire machine_timer_selected=MACHINE_TIMER_ENABLE && data_address>=32'h10000400 && data_address<=32'h10000410;
  reg [63:0] machine_mtime,machine_mtimecmp;
  wire machine_timer_pending=MACHINE_TIMER_ENABLE && machine_mtime>=machine_mtimecmp;
  reg [31:0] machine_timer_data;
  always @* begin
    case(data_address[4:2])
      0: machine_timer_data=machine_mtime[31:0];
      1: machine_timer_data=machine_mtime[63:32];
      2: machine_timer_data=machine_mtimecmp[31:0];
      3: machine_timer_data=machine_mtimecmp[63:32];
      4: machine_timer_data=SYSTEM_CLOCK_HZ;
      default: machine_timer_data=0;
    endcase
  end
  integer timer_byte;
  always @(posedge clk) begin
    if(!reset_n) begin machine_mtime<=0;machine_mtimecmp<=64'hffffffffffffffff;end
    else begin
      machine_mtime<=machine_mtime+64'd1;
      if(MACHINE_TIMER_ENABLE && !cpu_hold && data_write_enable && machine_timer_selected) begin
        for(timer_byte=0;timer_byte<4;timer_byte=timer_byte+1) begin
          if(data_write_strobe[timer_byte]) case(data_address[4:2])
            0:machine_mtime[timer_byte*8+:8]<=data_write_data[timer_byte*8+:8];
            1:machine_mtime[32+timer_byte*8+:8]<=data_write_data[timer_byte*8+:8];
            2:machine_mtimecmp[timer_byte*8+:8]<=data_write_data[timer_byte*8+:8];
            3:machine_mtimecmp[32+timer_byte*8+:8]<=data_write_data[timer_byte*8+:8];
            default:begin end
          endcase
        end
      end
    end
  end
  wire gamepad_local_selected=data_address[31:5]==27'h0800009;
  reg [31:0] cpu_gamepad_read_data;
  always @* begin
    cpu_gamepad_read_data=0;
    if(GAMEPAD_INPUT) case(data_address[4:0])
      5'h00:cpu_gamepad_read_data={15'd0,gamepad_connected,4'd0,gamepad_buttons};
      5'h04:cpu_gamepad_read_data=32'h49500100;
      5'h08:cpu_gamepad_read_data=gamepad_sequence;
      5'h0c:cpu_gamepad_read_data=gamepad_left;
      5'h10:cpu_gamepad_read_data=gamepad_right;
      5'h14:cpu_gamepad_read_data=gamepad_aux;
      default:cpu_gamepad_read_data=0;
    endcase
  end
  wire mmio_selected=!machine_timer_selected && data_address[31:16]==16'h1000 &&
      data_address!=32'h10000250 && data_address!=32'h1000011c && !gamepad_local_selected;
  // CPU-domain IO bypasses the mailbox, but must retain global MMIO order.
  // Normal work RAM still overlaps; the wall clock itself never stops.
  wire cpu_local_mmio=INDEPENDENT_CLOCKS && !mmio_selected && data_address[31:16]==16'h1000;
  reg [31:0] cpu_cycles;
  always @(posedge clk) if(!reset_n) cpu_cycles<=0;else cpu_cycles<=cpu_cycles+1;
  assign cpu_quiescent=cpu_hold && mmio_idle && !ddr_busy && !ram_store_pending;
  generate if(INDEPENDENT_CLOCKS) begin:g_mmio_cdc
    cpu_peripheral_cdc #(.SHARED_VIDEO_RESET(1),.POSTED_WRITES(1)) u_bridge(.peripheral_reset_n(peripheral_reset_n),.cpu_clk(clk),.peripheral_clk(peripheral_clk),.reset_n(common_reset_n),
      .cpu_hold(cpu_hold),.read_request(data_read_request && mmio_selected),
      .write_request(cpu_write_intent && mmio_selected),.address(data_address),
      .write_data(data_write_data),.write_mask(cpu_write_mask),.read_ready(mmio_read_ready),
      .write_ready(mmio_write_ready),.read_data(mmio_reply),.quiescent(mmio_idle),
      .peripheral_address(peripheral_address),.peripheral_write_data(peripheral_write_data),
      .peripheral_write_mask(peripheral_write_strobe),.peripheral_write_enable(peripheral_write_enable),
      .peripheral_write_ready(framebuffer_write_ready && audio_write_ready),.peripheral_read_data(peripheral_read_data));
  end else begin:g_mmio_native
    assign peripheral_address=data_address;assign peripheral_write_data=data_write_data;
    assign peripheral_write_strobe=data_write_strobe;assign peripheral_write_enable=data_write_enable && !machine_timer_selected;
    assign mmio_reply=0;assign mmio_read_ready=1;assign mmio_write_ready=1;assign mmio_idle=1;
  end endgenerate


  wire [31:0] audio_read_data, audio_host_read_data;
  generate if(AUDIO_ENABLE) begin : g_audio
    nes_audio #(.CLOCK_HZ(PERIPHERAL_CLOCK_HZ),.SERIAL_FRAME_MEAN(AUDIO_SERIAL_FRAME_MEAN)) u_audio(.clk(peripheral_clk),.reset_n(peripheral_reset_n),.cpu_hold(peripheral_hold),
        .pcm_host_address(pcm_host_address),.pcm_host_write(pcm_host_write),.pcm_host_data(pcm_host_data),
        .pcm_host_ready(pcm_host_ready),.pcm_host_read_data(pcm_host_read_data),
        .address(peripheral_address),.write_data(peripheral_write_data),
        .write_enable(peripheral_write_enable),.write_strobe(peripheral_write_strobe),
        .host_address(INDEPENDENT_CLOCKS ? 32'd0 : load_address),.read_data(audio_read_data),
        .host_read_data(audio_host_read_data),.write_ready(audio_write_ready),
        .audio_din(audio_din),.audio_bclk(audio_bclk),.audio_lrc(audio_lrc));
  end else begin : g_no_audio
    assign pcm_host_ready=0;assign pcm_host_read_data=0;
    assign audio_read_data=0;
    assign audio_host_read_data=0;
    assign audio_write_ready=1;
    assign audio_din=0;
    assign audio_bclk=0;
    assign audio_lrc=0;
  end endgenerate
  reg [31:0] data_read_data;
  wire [31:0] debug_pc;
  wire cpu_reset_n = reset_n && !(HOST_LOADER && cpu_hold);
  wire [31:0] program_read_data;
  wire [31:0] asset_read_data;
  wire asset_selected = USE_C_PROGRAM && HOST_LOADER &&
      data_address >= 32'h30000000 && data_address < 32'h3000c000;
  wire [31:0] ram_read_data;
  wire ram_selected = USE_C_PROGRAM && data_address[31:17] == 15'h1000;
  wire cpu_local_read_hint,cpu_local_read_hint_ready,cpu_local_load_hint_first;
  wire [31:0] cpu_local_read_address;
  generate
    if (USE_C_PROGRAM) begin : g_c_program
      graphics_c_rom #(.HOST_LOADER(HOST_LOADER)) u_rom (
          .clk(clk),
          .address(instruction_address),
          .instruction(local_instruction_data),
          .data_address(HOST_LOADER && cpu_hold ? load_address : data_address),
          .read_data(program_read_data),
          .load_write(HOST_LOADER && cpu_hold && load_write),
          .load_address(load_address),
          .load_data(load_data)
      );
      // 128 KiB工作RAM，0x20000000..0x2001ffff。内容由启动程序初始化。
      // 同步读与CPU已有MEMORY_WAIT匹配；按字节写支持SB/SH/SW。
      // Keep the unconditional read; the implementation hook excludes unsafe power gating.
      (* ram_style = "block" *) reg [31:0] work_ram [0:32767];
      reg [31:0] ram_data;
      reg ram_write_pending;
      assign ram_store_pending=ram_write_pending;
      reg [14:0] ram_write_address;
      reg [31:0] ram_write_data;
      reg [3:0] ram_write_strobe;
      // A pending/current store may not be visible to this early read yet.
      // Decline the hint and retain the original byte-forwarded load path.
      assign cpu_local_read_hint_ready=cpu_reset_n && !ram_write_pending &&
          !(ram_selected && data_write_enable) && cpu_local_read_address[31:17]==15'h1000;
      wire ram_hint_selected=cpu_local_read_hint && cpu_local_read_hint_ready;
      // Port A always serves the executing load. Port B speculates only when
      // its pending-store write owner is absent; a tag identifies the returned word.
      wire [14:0] ram_hint_port_index=ram_write_pending ? ram_write_address : cpu_local_read_address[16:2];
      reg [31:0] ram_hint_data;
      reg [14:0] ram_hint_index;
      reg ram_hint_valid;
      always @(posedge clk) begin
        ram_hint_valid <= cpu_reset_n && ram_hint_selected;
        ram_hint_index <= cpu_local_read_address[16:2];
      end
      // The CPU records the actual EX->MEM admission beside the MEM address.
      // The next cycle uses Port A; a newer Port-B preview cannot steal ownership.
      wire [31:0] ram_read_payload=cpu_local_load_hint_first ? ram_hint_data : ram_data;
`ifndef SYNTHESIS
      wire [31:0] ram_legacy_payload=ram_hint_valid && ram_hint_index==data_address[16:2] ? ram_hint_data : ram_data;
      integer hint_byte;
      always @(posedge clk) if(cpu_reset_n) begin
        if(cpu_local_load_hint_first && (!ram_hint_valid || ram_hint_index!==data_address[16:2]))
          $fatal(1,"MEM_HINT_FIRST_OWNER_MISMATCH");
        if(data_read_request && ram_selected && u_cpu.mem_complete)
          for(hint_byte=0;hint_byte<4;hint_byte=hint_byte+1)
            if(!ram_forward_strobe[hint_byte] &&
               ram_read_payload[hint_byte*8+:8]!==ram_legacy_payload[hint_byte*8+:8])
              $fatal(1,"MEM_HINT_LEGACY_VALUE_MISMATCH");
      end
`endif
      // Register the store before BRAM: async CPU reset must not drive RAM enables.
      always @(posedge clk) begin
        if (!cpu_reset_n) ram_write_pending <= 0;
        else ram_write_pending <= ram_selected && data_write_enable;
        if (ram_selected && data_write_enable) begin
          ram_write_address <= data_address[16:2];
          ram_write_data <= data_write_data;
          ram_write_strobe <= data_write_strobe;
        end
      end
      always @(posedge clk) ram_data <= work_ram[data_address[16:2]];
      always @(posedge clk) begin
        ram_hint_data <= work_ram[ram_hint_port_index];
        if (ram_write_pending) begin
          if (ram_write_strobe[0]) work_ram[ram_hint_port_index][7:0] <= ram_write_data[7:0];
          if (ram_write_strobe[1]) work_ram[ram_hint_port_index][15:8] <= ram_write_data[15:8];
          if (ram_write_strobe[2]) work_ram[ram_hint_port_index][23:16] <= ram_write_data[23:16];
          if (ram_write_strobe[3]) work_ram[ram_hint_port_index][31:24] <= ram_write_data[31:24];
        end
      end
      // 保留BRAM无条件同步读。紧邻Store的Load在写RAM的同一沿读取旧字，按字节转发待写值。
      reg [3:0] ram_forward_strobe;
      reg [31:0] ram_forward_data;
      always @(posedge clk) begin
        ram_forward_strobe <= DDR_PROGRAM && DDR_DECODE_STORE && cpu_reset_n && ram_write_pending && ram_selected &&
            ram_write_address == data_address[16:2] ? ram_write_strobe : 4'b0000;
        ram_forward_data <= ram_write_data;
      end
      assign ram_read_data = {ram_forward_strobe[3] ? ram_forward_data[31:24] : ram_read_payload[31:24],
          ram_forward_strobe[2] ? ram_forward_data[23:16] : ram_read_payload[23:16],
          ram_forward_strobe[1] ? ram_forward_data[15:8] : ram_read_payload[15:8],
          ram_forward_strobe[0] ? ram_forward_data[7:0] : ram_read_payload[7:0]};
    end else begin : g_asm_program
      graphics_program_rom u_rom (
          .address(instruction_address),
          .instruction(local_instruction_data)
      );
      assign cpu_local_read_hint_ready=1'b0;
      assign ram_store_pending=0;
      assign program_read_data = 32'd0;
      assign ram_read_data = 32'd0;
    end
  endgenerate
  generate
    if (USE_C_PROGRAM && HOST_LOADER) begin : g_assets
      game_asset_ram u_assets (
          .clk(clk), .read_address(cpu_hold ? load_address : data_address),
          .read_data(asset_read_data), .load_write(cpu_hold && load_write),
          .load_address(load_address), .load_data(load_data)
      );
    end else begin : g_no_assets
      assign asset_read_data = 32'd0;
    end
  endgenerate
  assign load_read_data = !INDEPENDENT_CLOCKS && AUDIO_ENABLE && load_address[31:8]==24'h100003 ? audio_host_read_data :
      load_address >= 32'h30000000 && load_address < 32'h3000c000 ?
      asset_read_data : program_read_data;

  wire cpu_data_address_is_ddr;
  wire instruction_next_hint;
  wire [31:0] instruction_next_address;
  wire cache_ready, cache_error;
  wire [31:0] cache_read_data;
  wire cache_refill_start, cache_refill_write, cache_refill_commit;
  wire [31:0] cache_refill_address, cache_refill_data;
  wire instruction_buffer_hit;
  wire [31:0] instruction_buffer_data;
  wire external_instruction = DDR_PROGRAM && instruction_address[31:24]==8'h80;
  wire external_data = DDR_PROGRAM && (DDR_FAST_ADDRESS_REGIONS ? cpu_data_address_is_ddr : data_address[31:24]==8'h80);
  wire [31:0] cache_address = data_read_request && external_data ? {8'd0,data_address[23:2],2'b00} :
      {8'd0,instruction_address[23:0]};
  reg ddr_data_valid,ddr_data_error;
  reg [31:0] ddr_data_payload;
  // Only the owned data response is delayed. Instruction responses and scan
  // timing stay immediate. Suppress replay while the CPU consumes this register.
  always @(posedge clk) begin
    if(!reset_n || cpu_hold || ddr_flush || ddr_fault) begin ddr_data_valid<=1'b0;ddr_data_error<=0;end
    else if(ddr_data_valid) begin
      if(data_read_request && external_data) ddr_data_valid<=1'b0;
    end else if(data_read_request && external_data && cache_ready) begin
      ddr_data_payload<=cache_read_data;ddr_data_error<=cache_error;ddr_data_valid<=1'b1;
    end
  end
  wire external_request = (instruction_request && external_instruction && !instruction_buffer_hit) ||
      (data_read_request && external_data && !ddr_data_valid);
  wire external_allowed = ddr_enable && cache_address < ddr_size;
  wire cache_data_owner = data_read_request && external_data;
  // Both consumers require !cache_data_owner, so their address is the PC.
  // Keep data/fault request bounds on the original shared address path.
  wire instruction_fill_allowed = DDR_SPLIT_INSTRUCTION_BOUNDS ?
      (ddr_enable && {8'd0,instruction_address[23:0]} < ddr_size) : external_allowed;
`ifndef SYNTHESIS
  integer instruction_bounds_checks=0;
  always @(posedge clk) begin
    if (DDR_SPLIT_INSTRUCTION_BOUNDS && reset_n && !cpu_hold && !ddr_flush && !cache_data_owner) begin
      if (instruction_fill_allowed !== external_allowed) $fatal(1,"Instruction-only bounds changed idle-owner qualification");
      if (instruction_bounds_checks==0) $display("INSTRUCTION_BOUNDS_ORACLE_ACTIVE");
      instruction_bounds_checks <= instruction_bounds_checks+1;
    end
  end
`endif
  wire instruction_resident_hit;
  wire cache_scan_valid;
  wire [31:0] cache_scan_address, cache_scan_data;
  wire scan_same_line = external_instruction && instruction_address[23:6] == cache_scan_address[23:6];
  wire scan_publish = DDR_INSTRUCTION_LINE_FILL && DDR_INSTRUCTION_BUFFER_WORDS >= 16 &&
      cache_scan_valid && scan_same_line && !cache_data_owner && ddr_enable &&
      cache_scan_address < ddr_size && !ddr_fault && !cpu_hold && !ddr_flush;
  wire scan_match = scan_publish && instruction_address[1:0] == 0 &&
      instruction_address[23:0] == cache_scan_address[23:0];
  assign instruction_buffer_hit = instruction_resident_hit || scan_match;
  wire [31:0] cache_next_address = {8'd0,DDR_HANDOFF_HINT ? instruction_next_address[23:0] : instruction_address[23:0]};
  wire cache_next_request = (DDR_HANDOFF_HINT ? instruction_next_hint :
      (DDR_LOAD_HANDOFF && cache_data_owner && instruction_request && external_instruction)) &&
      ddr_enable && cache_next_address < ddr_size && !ddr_fault && !cpu_hold;
  assign instruction_data = external_instruction ?
      (instruction_buffer_hit ? instruction_buffer_data : cache_read_data) : local_instruction_data;
  // Static targets are computed on refill, outside the resident-hit fetch feedback path.
  function [31:0] predict_instruction_next;
    input [31:0] pc, instruction;
    reg [31:0] displacement;
    begin
      if (instruction[6:0]==7'h6f)
        displacement={{11{instruction[31]}},instruction[31],instruction[19:12],instruction[20],instruction[30:21],1'b0};
      else if (instruction[6:0]==7'h63 && instruction[31])
        displacement={{19{instruction[31]}},instruction[31],instruction[7],instruction[30:25],instruction[11:8],1'b0};
      else displacement=32'd4;
      predict_instruction_next=pc+displacement;
    end
  endfunction
  generate
    if (DDR_PROGRAM && DDR_INSTRUCTION_BUFFER_WORDS > 0) begin : g_instruction_buffer
      localparam BITS = $clog2(DDR_INSTRUCTION_BUFFER_WORDS);
      wire [DDR_INSTRUCTION_BUFFER_WORDS-1:0] valid;
      wire selected_valid;
      wire pair_selected_valid;
      // Payload-only lookup follows the registered fetch owner. Request, tag,
      // permission and fill addressing retain their original index below.
      wire [BITS-1:0] owned_payload_index=pipeline_permission_address[BITS+1:2];
      wire [BITS-1:0] pair_index=index+{{(BITS-1){1'b0}},1'b1};
      wire [BITS-1:0] owned_even_row=((owned_payload_index >> 1)+owned_payload_index[0]) & (DDR_INSTRUCTION_BUFFER_WORDS/2-1);
      (* ram_style = "distributed" *) reg [31:0] words_even [0:DDR_INSTRUCTION_BUFFER_WORDS/2-1];
      (* ram_style = "distributed" *) reg [31:0] words_odd [0:DDR_INSTRUCTION_BUFFER_WORDS/2-1];
      wire [31:0] resident_even_word=words_even[owned_even_row];
      wire [31:0] resident_odd_word=words_odd[owned_payload_index >> 1];
`ifndef SYNTHESIS
      // Read-only compatibility view for existing simulation observers.
      wire [31:0] words [0:DDR_INSTRUCTION_BUFFER_WORDS-1];
      genvar view_word;
      for(view_word=0;view_word<DDR_INSTRUCTION_BUFFER_WORDS;view_word=view_word+1) begin:g_word_view
        assign words[view_word]=(view_word%2) ? words_odd[view_word/2] : words_even[view_word/2];
      end
`endif
      wire [23-BITS-2:0] resident_tag;
`ifndef SYNTHESIS
      wire [23-BITS-2:0] tags [0:DDR_INSTRUCTION_BUFFER_WORDS-1];
`endif
      (* ram_style = "distributed" *) reg [31:0] prediction_targets [0:DDR_INSTRUCTION_BUFFER_WORDS-1];
      wire [BITS-1:0] index = instruction_address[BITS+1:2];
      // Invalid payload values are ignored. Hit/tag/metadata qualification belongs
      // in ready, rather than the wide instruction/prediction data muxes.
      assign instruction_resident_word=owned_payload_index[0] ? resident_odd_word : resident_even_word;
      // Word-aligned pairs stay within one16-word row. The existing banks
      // supply odd->next-even or even->odd; lane1 still requires full admission.
      assign pair_word_data=DDR_ALIGNED_PAIR_SUPPLY ?
          (owned_payload_index[0] ? resident_even_word : resident_odd_word) : 32'd0;
      assign pair_word_valid=DDR_ALIGNED_PAIR_SUPPLY && reset_n &&
          instruction_resident_hit && instruction_prediction_metadata_ready &&
          instruction_address[1:0]==0 && instruction_address[5:2]!=4'hf && index!={BITS{1'b1}} && pair_selected_valid && ddr_size>=8 &&
          permission_offset_le(instruction_address[23:0],ddr_size-8);
      assign instruction_resident_prediction=prediction_targets[index];
      wire prediction_write;
      wire [BITS-1:0] prediction_write_index;
      wire [31:0] prediction_write_pc,prediction_write_instruction;
      reg metadata_pending;
      reg [BITS-1:0] metadata_index;
      reg [31:0] metadata_pc,metadata_instruction;
      // A freshly replaced instruction may precede its prediction by one clock.
      assign instruction_prediction_metadata_ready = !(metadata_pending && metadata_index==index);
      assign instruction_cached_prediction = instruction_resident_hit && instruction_prediction_metadata_ready ?
          prediction_targets[index] : instruction_address+4;
      always @(posedge clk) begin
        if(!reset_n || cpu_hold || ddr_flush) metadata_pending<=0;
        else begin
          metadata_pending<=prediction_write;
          if(prediction_write) begin
            metadata_index<=prediction_write_index;
            metadata_pc<=prediction_write_pc;
            metadata_instruction<=prediction_write_instruction;
          end
          if(metadata_pending)
            prediction_targets[metadata_index] <= predict_instruction_next(metadata_pc,metadata_instruction);
        end
      end
      assign instruction_buffer_data = scan_match ? cache_scan_data : instruction_resident_word;
      assign instruction_resident_hit = selected_valid && resident_tag == instruction_address[23:BITS+2] &&
          external_instruction && instruction_address[1:0] == 0 && ddr_enable &&
          {8'd0,instruction_address[23:0]} < ddr_size && !ddr_fault && !cpu_hold && !ddr_flush;
      wire fill_word = instruction_request && external_instruction && !instruction_buffer_hit &&
          !cache_data_owner && cache_ready && !cache_error && !ddr_fault && instruction_fill_allowed;
      if (DDR_INSTRUCTION_LINE_FILL && DDR_INSTRUCTION_BUFFER_WORDS >= 16) begin : g_line_fill
        reg refill_instruction;
        // A cache-hit word owns its payload/address until the I-buffer write.
        // Common reset/HOLD/flush/fault or a new refill cancels this speculative copy.
        reg queued_fill_valid;
        reg [BITS-1:0] queued_fill_index;
        reg [31:0] queued_fill_pc,queued_fill_data;
        // ISSUE is registered. Suppress speculative copies for its whole duration,
        // before ARREADY arrives; actual refill start/commit still use handshakes.
        wire queued_fill_write=queued_fill_valid && !ddr_arvalid &&
            !cache_refill_write && !cache_refill_commit && !scan_publish;
        always @(posedge clk) begin
          if(!reset_n || cpu_hold || ddr_flush || ddr_fault || cache_refill_start) queued_fill_valid<=0;
          else begin
            if(queued_fill_write) queued_fill_valid<=0;
            if(fill_word && !queued_fill_valid) begin
              queued_fill_valid<=1;
              queued_fill_index<=index;
              queued_fill_pc<=instruction_address;
              queued_fill_data<=cache_read_data;
            end
          end
        end
        wire start_instruction = !cache_data_owner && instruction_request && external_instruction &&
            !instruction_buffer_hit && instruction_fill_allowed && !ddr_fault;
        wire [BITS-1:0] refill_index = cache_refill_address[BITS+1:2];
        wire [BITS-1:0] refill_line_index = refill_index & ~32'h0000000f;
        wire fill_line_word = refill_instruction && cache_refill_write;
        // scan只在IDLE；queued_fill_write在LOOKUP/RESPONSE、fill_line_word在FILL，天然互斥。
        // Scan is speculative: register its owned payload before the tag/mask
        // read-modify-write. Architectural cache responses remain unchanged.
        reg scan_pending;
        reg [31:0] scan_pending_address,scan_pending_data;
        always @(posedge clk) begin
          if(!reset_n || cpu_hold || ddr_flush || ddr_fault || cache_refill_start)
            scan_pending<=1'b0;
          else begin
            scan_pending<=scan_publish;
            if(scan_publish) begin
              scan_pending_address<=cache_scan_address;
              scan_pending_data<=cache_scan_data;
            end
          end
        end
        // Refill and the older queued word win; a conflicting speculative
        // scan copy may be discarded and fetched again through the cache.
        wire scan_fill = scan_pending && !queued_fill_valid &&
            !ddr_arvalid && !cache_refill_write && !cache_refill_commit &&
            !ddr_fault;
        wire [BITS-1:0] scan_index = scan_pending_address[BITS+1:2];
        wire [BITS-1:0] write_index = fill_line_word ? refill_index : scan_fill ? scan_index : queued_fill_index;
        assign prediction_write=reset_n && !cpu_hold && !ddr_flush && (fill_line_word || queued_fill_write || scan_fill);
        assign prediction_write_index=write_index;
        assign prediction_write_pc=fill_line_word ? {8'h80,cache_refill_address[23:0]} :
            scan_fill ? {8'h80,scan_pending_address[23:0]} : queued_fill_pc;
        assign prediction_write_instruction=fill_line_word ? cache_refill_data : scan_fill ? scan_pending_data : queued_fill_data;
        always @(posedge clk) begin
          if (!reset_n || cpu_hold || ddr_flush) refill_instruction <= 1'b0;
          else if (cache_refill_start) refill_instruction <= start_instruction;
        end
        localparam ROWS = DDR_INSTRUCTION_BUFFER_WORDS / 16;
        localparam ROW_BITS = BITS > 4 ? BITS-4 : 1;
        reg [ROWS-1:0] row_active;
        // One tag per16-word row shortens the fetch hit path and removes redundant RAM.
        (* ram_style = "distributed" *) reg [23-BITS-2:0] line_tags [0:ROWS-1];
      assign resident_tag=line_tags[index >> 4];
`ifndef SYNTHESIS
        genvar tag_word;
        for(tag_word=0;tag_word<DDR_INSTRUCTION_BUFFER_WORDS;tag_word=tag_word+1) begin:g_line_tag_view
          assign tags[tag_word]=line_tags[tag_word/16];
        end
`endif
        wire [23-BITS-2:0] incoming_line_tag=fill_line_word ? cache_refill_address[23:BITS+2] :
            scan_fill ? scan_pending_address[23:BITS+2] : queued_fill_pc[23:BITS+2];
        (* ram_style = "distributed" *) reg [15:0] valid_masks [0:ROWS-1];
        wire [ROW_BITS-1:0] read_row = index >> 4;
        wire [ROW_BITS-1:0] refill_row = refill_index >> 4;
        wire row_start = cache_refill_start && start_instruction;
        wire row_commit = cache_refill_commit && refill_instruction && !ddr_fault;
        wire [ROW_BITS-1:0] word_write_row = write_index >> 4;
        wire [ROW_BITS-1:0] mask_write_row = queued_fill_write || scan_fill ? word_write_row : refill_row;
        wire [15:0] mask_word_data = (row_active[word_write_row] && line_tags[word_write_row]==incoming_line_tag ? valid_masks[word_write_row] : 16'd0) |
            (16'h0001 << write_index[3:0]);
        assign selected_valid = row_active[read_row] && valid_masks[read_row][index[3:0]];
        assign pair_selected_valid = row_active[read_row] && valid_masks[read_row][pair_index[3:0]];
        // valid仅供整行可见性观察；实际命中使用单个动态RAM读端口。
`ifndef SYNTHESIS
        genvar visible_row;
        for (visible_row=0; visible_row<ROWS; visible_row=visible_row+1) begin : g_visible_valid
          assign valid[visible_row*16 +: 16] = row_active[visible_row] ? valid_masks[visible_row] : 16'd0;
        end
`endif
        always @(posedge clk) begin
          // 行有效位可同步清零，掩码RAM不复位；首次逐字写入忽略失效行的旧掩码。
          if (!reset_n || cpu_hold || ddr_flush) row_active <= 0;
          else if (row_commit) row_active[refill_row] <= 1'b1;
          else if (queued_fill_write || scan_fill) row_active[word_write_row] <= 1'b1;
          else if (row_start) row_active[refill_row] <= 1'b0;
          // scan仅在IDLE，正常逐字/整行填充优先，掩码RAM仍保留一个写端口。
          if (reset_n && !cpu_hold && !ddr_flush && (row_start || row_commit || queued_fill_write || scan_fill))
            valid_masks[mask_write_row] <= row_commit ? 16'hffff : queued_fill_write || scan_fill ? mask_word_data : 16'd0;
          if (reset_n && !cpu_hold && !ddr_flush && (fill_line_word || queued_fill_write || scan_fill)) begin
            if(write_index[0]) words_odd[write_index >> 1] <= fill_line_word ? cache_refill_data : scan_fill ? scan_pending_data : queued_fill_data;
            else words_even[write_index >> 1] <= fill_line_word ? cache_refill_data : scan_fill ? scan_pending_data : queued_fill_data;
            line_tags[word_write_row] <= incoming_line_tag;
          end
        end
      end else begin : g_word_fill
      reg [DDR_INSTRUCTION_BUFFER_WORDS-1:0] word_valid;
      (* ram_style = "distributed" *) reg [23-BITS-2:0] word_tags [0:DDR_INSTRUCTION_BUFFER_WORDS-1];
      assign resident_tag=word_tags[index];
`ifndef SYNTHESIS
      genvar tag_word;
      for(tag_word=0;tag_word<DDR_INSTRUCTION_BUFFER_WORDS;tag_word=tag_word+1) begin:g_word_tag_view
        assign tags[tag_word]=word_tags[tag_word];
      end
`endif
      assign valid = word_valid;
      assign selected_valid = word_valid[index];
      assign pair_selected_valid = word_valid[pair_index] && word_tags[pair_index]==instruction_address[23:BITS+2];
      assign prediction_write=reset_n && !cpu_hold && !ddr_flush && fill_word;
      assign prediction_write_index=index;
      assign prediction_write_pc=instruction_address;
      assign prediction_write_instruction=cache_read_data;
      always @(posedge clk) begin
        if (!reset_n || cpu_hold || ddr_flush) word_valid <= 0;
        else if (fill_word) begin
          if(index[0]) words_odd[index >> 1] <= cache_read_data;
          else words_even[index >> 1] <= cache_read_data;
          word_tags[index] <= instruction_address[23:BITS+2];
          word_valid[index] <= 1'b1;
        end
      end
      end
      initial if (DDR_INSTRUCTION_BUFFER_WORDS < 2 ||
          (DDR_INSTRUCTION_BUFFER_WORDS & (DDR_INSTRUCTION_BUFFER_WORDS-1)) != 0)
          $error("Instruction buffer word count must be a power of two >= 2");
      initial if (DDR_INSTRUCTION_LINE_FILL && DDR_INSTRUCTION_BUFFER_WORDS < 16)
          $error("Instruction line fill requires >= 16 buffer words");
    end else begin : g_no_instruction_buffer
      assign pair_word_valid=1'b0;
      assign pair_word_data=32'd0;
      assign instruction_prediction_metadata_ready = 1'b0;
      assign instruction_cached_prediction = instruction_address+4;
      assign instruction_resident_word=32'd0;
      assign instruction_resident_prediction=instruction_address+4;
      assign instruction_resident_hit = 1'b0;
      assign instruction_buffer_data = 32'd0;
    end
  endgenerate
  generate
    if (DDR_PROGRAM) begin : g_ddr
      ddr_read_cache #(.CACHE_LINES(DDR_CACHE_LINES), .CACHE_WAYS(DDR_CACHE_WAYS), .EARLY_HIT(DDR_EARLY_HIT), .RESPONSE_CHAIN(DDR_LOAD_HANDOFF), .CHAIN_READ_AHEAD(DDR_HANDOFF_HINT), .SEPARATE_SCAN_READ(DDR_DECOUPLED_READ_HINT), .DECOUPLED_READ_HINT(DDR_DECOUPLED_READ_HINT), .HIT_LINE_SCAN(DDR_INSTRUCTION_LINE_FILL && DDR_INSTRUCTION_BUFFER_WORDS >= 16), .SCAN_NEXT_LINE(1)) u_cache (
          .clk(clk), .reset_n(reset_n), .flush(ddr_flush || cpu_hold),
          .ddr_base(ddr_base), .request(external_request && external_allowed && !ddr_fault && !cpu_hold),
          .address(cache_address), .ready(cache_ready), .read_data(cache_read_data),
          .next_read_hint(cache_next_request), .next_request(cache_next_request && !instruction_buffer_hit), .next_address(cache_next_address),
          .instruction_hit_request(instruction_request && external_instruction && !cache_data_owner),
          .scan_continue(scan_same_line && !ddr_fault && !cpu_hold),
          .scan_valid(cache_scan_valid), .scan_address(cache_scan_address), .scan_data(cache_scan_data),
          .refill_start(cache_refill_start), .refill_write(cache_refill_write),
          .refill_address(cache_refill_address), .refill_data(cache_refill_data), .refill_commit(cache_refill_commit),
          .error(cache_error), .busy(ddr_busy), .m_axi_araddr(ddr_araddr),
          .m_axi_arlen(ddr_arlen), .m_axi_arsize(ddr_arsize), .m_axi_arburst(ddr_arburst),
          .m_axi_arcache(ddr_arcache), .m_axi_arprot(ddr_arprot),
          .m_axi_arvalid(ddr_arvalid), .m_axi_arready(ddr_arready),
          .m_axi_rdata(ddr_rdata), .m_axi_rresp(ddr_rresp), .m_axi_rlast(ddr_rlast),
          .m_axi_rvalid(ddr_rvalid), .m_axi_rready(ddr_rready)
      );
    end else begin : g_no_ddr
      assign cache_scan_valid=1'b0;
      assign cache_scan_address=32'd0;
      assign cache_scan_data=32'd0;
      assign cache_refill_start=1'b0;
      assign cache_refill_write=1'b0;
      assign cache_refill_commit=1'b0;
      assign cache_refill_address=32'd0;
      assign cache_refill_data=32'd0;
      assign cache_ready=1'b0;
      assign cache_error=1'b0;
      assign cache_read_data=32'd0;
      assign ddr_busy=1'b0;
      assign ddr_araddr=32'd0;
      assign ddr_arlen=8'd0;
      assign ddr_arsize=3'd2;
      assign ddr_arburst=2'b01;
      assign ddr_arcache=4'd0;
      assign ddr_arprot=3'd0;
      assign ddr_arvalid=1'b0;
      assign ddr_rready=1'b0;
    end
  endgenerate
  always @(posedge clk) begin
    if (!reset_n || cpu_hold || ddr_flush) ddr_fault <= 1'b0;
    // Architectural permission traps recover through mtvec; AXI faults retain the old diagnostic latch for now.
    else if (DDR_PROGRAM && ((external_request && !external_allowed) ||
        (instruction_request && instruction_address>=32'h01000 && !external_instruction))) ddr_fault <= 1'b1;
  end
  // Failed read responses are architectural fault tokens, never sticky transport stops.
  wire cpu_instruction_access_error=instruction_request && external_instruction && !cache_data_owner && cache_ready && cache_error;
  // Experimental candidate: preserve all peripheral/cache wiring, replace only the CPU.
  wire pipeline_fetch_permitted = pipeline_permission_address[1:0]==0 &&
      (pipeline_permission_address<32'h01000 ||
       (DDR_PROGRAM && pipeline_permission_address[31:24]==8'h80 && ddr_enable &&
        {8'd0,pipeline_permission_address[23:0]}<ddr_size));
  wire pipeline_next_fetch_permitted = instruction_next_address[1:0]==0 &&
      (instruction_next_address<32'h01000 ||
       (DDR_PROGRAM && instruction_next_address[31:24]==8'h80 && ddr_enable &&
        {8'd0,instruction_next_address[23:0]}<ddr_size));
  rv32i_pipeline_core #(
      .ALIGNED_DUAL_ALU(CPU_DUAL_ALU),
      .DRAIN_PERIPHERAL_WRITES(INDEPENDENT_CLOCKS),
      .SEPARATE_FETCH_PERMISSION(1),
      .EX_RESULT_BYPASS(1),
      .ID_INDEPENDENT_LOW_MUL(1),.MEM_BRANCH_RESOLVE(1),.REDIRECT_RESPONSE_HANDOFF(1),.EMPTY_FETCH_BYPASS(1),.CORRELATED_DIRECTION(1),
      .SYNC_RESET(DDR_PROGRAM && DDR_SYNC_RESET),
      .SEPARATE_LOCAL_HINT_PORT(USE_C_PROGRAM && DDR_PROGRAM),.DYNAMIC_BRANCH_PREDICTION(1),.INDIRECT_TARGET_CACHE(1),.PREFETCH_LOCAL_RAM(USE_C_PROGRAM && DDR_PROGRAM),.OVERLAP_LOCAL_LOAD(1),.PREDECODED_PREDICTION(1),.STATIC_CONTROL_PREDICTION(1),.CHECK_FETCH_PERMISSION(1),.LOCAL_FETCH_WAIT(USE_C_PROGRAM ? 2 : 0)
  ) u_cpu (
      .data_permission_address(cpu_permission_address),.data_permission_write(cpu_permission_write),
      .data_access_permitted(cpu_memory_permitted),
      .instruction_access_error(cpu_instruction_access_error),.data_access_error(external_data && ddr_data_error),
      .irq_external(MACHINE_IRQ_ENABLE ? machine_irq_external : 1'b0),
      .irq_software(MACHINE_IRQ_ENABLE ? machine_irq_software : 1'b0),
      .irq_timer((MACHINE_IRQ_ENABLE ? machine_irq_timer : 1'b0) || machine_timer_pending),
      .platform_time(machine_mtime),.peripheral_quiescent(mmio_idle),
      .clk(clk),
      .reset_n(cpu_reset_n),
      .instruction_address(instruction_address),
      .instruction_pair_valid(pair_word_valid),.instruction_pair_data(pair_word_data),
      .instruction_permission_address(pipeline_permission_address),
      .instruction_next_permitted(pipeline_next_fetch_permitted),
      .instruction_permitted(pipeline_fetch_permitted),.instruction_fault(pipeline_instruction_fault),
      .instruction_data(external_instruction && DDR_INSTRUCTION_BUFFER_WORDS>0 ?
          instruction_resident_word : instruction_data),
      .instruction_prediction(external_instruction && DDR_INSTRUCTION_BUFFER_WORDS>0 ?
          instruction_resident_prediction : instruction_cached_prediction),
      .instruction_request(instruction_request),
      .instruction_next_hint(instruction_next_hint),
      .instruction_next_address(instruction_next_address),
      .instruction_ready(cpu_instruction_access_error || (!DDR_PROGRAM || !ddr_fault) &&
          (external_instruction ? (DDR_INSTRUCTION_BUFFER_WORDS>0 ?
              (instruction_resident_hit && instruction_prediction_metadata_ready) :
              (instruction_buffer_hit || (cache_ready && !cache_error && !(DDR_LOAD_HANDOFF && cache_data_owner)))) :
              (!DDR_PROGRAM || instruction_address<32'h01000))),
      .local_read_hint(cpu_local_read_hint),.local_read_address(cpu_local_read_address),
      .local_read_hint_ready(cpu_local_read_hint_ready),.local_load_hint_first(cpu_local_load_hint_first),
      .local_load_bypass_data(ram_read_data),
      .data_address(data_address),
      .data_address_is_ddr(cpu_data_address_is_ddr),
      .data_write_data(data_write_data),
      .data_write_strobe(data_write_strobe),.data_write_intent(cpu_write_intent),.data_write_mask(cpu_write_mask),
      .data_write_enable(data_write_enable),
      .data_write_ready(cpu_local_mmio ? mmio_idle : machine_timer_selected ? 1'b1 : INDEPENDENT_CLOCKS ? (mmio_selected ? mmio_write_ready : 1'b1) : (framebuffer_write_ready && audio_write_ready)),
      .data_read_data(data_read_data),
      .data_read_request(data_read_request),
      .data_read_ready(cpu_local_mmio ? mmio_idle : INDEPENDENT_CLOCKS && mmio_selected ? mmio_read_ready : (!external_data || (ddr_data_valid && !ddr_fault))),
      .debug_pc(debug_pc),
      .debug_instruction(),
      .debug_state(),
      .debug_reg_write(),
      .debug_reg_write_addr(),
      .debug_reg_write_data(),
      .retire_valid(),.retire_pc(),.retire_instruction()
  );

`ifndef SYNTHESIS
  always @(posedge clk) if(cpu_reset_n && instruction_request && instruction_resident_hit) begin
    if(instruction_prediction_metadata_ready &&
        instruction_cached_prediction !== predict_instruction_next(instruction_address,instruction_data))
      $fatal(1,"Prediction metadata does not match the resident instruction/address");
    if(!instruction_prediction_metadata_ready && instruction_cached_prediction !== instruction_address+4)
      $fatal(1,"Pending prediction metadata did not fall back to sequential speculation");
  end
`endif
  // Shadow registers are writable; the displayed state changes only in blanking.
  reg [10:0] shadow_x, live_x, shadow_w, live_w;
  reg [9:0] shadow_y, live_y, shadow_h, live_h;
  reg [23:0] shadow_color, live_color, shadow_bg, live_bg;
  reg pending;
  reg [31:0] frame_count;
  assign ps_frame_count = frame_count;
  assign ps_cpu_pc = debug_pc;
  reg prev_vs, prev_de;
  reg [10:0] pixel_x;
  reg [9:0] pixel_y;
  wire frame_edge = vs_in && !prev_vs;  // Negative VS: trailing edge, still blanking.
  assign led = ~{pending, frame_count[5:3]};
  assign ps_game_position = {6'd0, live_y, 5'd0, live_x};

  wire [31:0] framebuffer_read_data;
  wire [23:0] framebuffer_rgb;
  wire frame_presented, fps_panel, fps_ink;
  wire [23:0] fps_rgb;
  reg [23:0] fps_rgb_d;
  reg fps_panel_d, fps_ink_d;
  framebuffer_engine #(.NATIVE800(NATIVE800),.REGISTER_DRAW_START(GPU_REGISTER_DRAW_START),.REGISTER_TILE_START(GPU_REGISTER_TILE_START),.DDR_BITMAP(DDR_BITMAP),.FAST_TILE_ROW(DDR_TILE_ROW),.COMPACT_FRAME(COMPACT_FRAME),.DDR_FRAME(DDR_FRAME)) u_framebuffer (
      .clk(peripheral_clk),
      .reset_n(peripheral_reset_n),
      .frame_edge(frame_edge),
      .address(peripheral_address),
      .write_data(peripheral_write_data),
      .write_strobe(peripheral_write_strobe),
      .write_enable(peripheral_write_enable),
      .read_data(framebuffer_read_data),
      .scan_de(de_in),
      .scan_x(pixel_x),
      .scan_y(pixel_y),
      .scan_rgb(framebuffer_rgb), .frame_presented(frame_presented),.write_ready(framebuffer_write_ready),
      .asset_base(INDEPENDENT_CLOCKS ? video_asset_base : ddr_base),.asset_size(INDEPENDENT_CLOCKS ? video_asset_size : ddr_size),.asset_enable(INDEPENDENT_CLOCKS ? video_asset_enable : ddr_enable),
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
      .frame_bready(frame_bready)
  );
  generate
    if (SHOW_FPS) begin : g_fps
      game_fps #(.CLOCK_HZ(PERIPHERAL_CLOCK_HZ)) u_fps (
          .clk(peripheral_clk), .reset_n(peripheral_reset_n && !(HOST_LOADER && peripheral_hold)), .presented(frame_presented),
          .frame_edge(frame_edge), .x(pixel_x), .y(pixel_y),
          .panel(fps_panel), .ink(fps_ink),.osd_rgb(fps_rgb),.present_count(ps_present_count),
          .window_frames(ps_fps_frames),.window_id(ps_fps_id),.window_valid(),.display_status(ps_fps_status)
      );
    end else begin : g_no_fps
      assign fps_panel=0;
      assign fps_ink=0;
      assign fps_rgb=0;
      assign ps_present_count=0;assign ps_fps_frames=0;assign ps_fps_id=0;assign ps_fps_status=0;
    end
  endgenerate
  reg hs_d, vs_d, de_d, border_d;

  always @(*) begin
    case (peripheral_address)
      32'h1000_0100: peripheral_read_data = {21'd0, shadow_x};
      32'h1000_0104: peripheral_read_data = {22'd0, shadow_y};
      32'h1000_0108: peripheral_read_data = {6'd0, shadow_h, 5'd0, shadow_w};
      32'h1000_010c: peripheral_read_data = {8'd0, shadow_color};
      32'h1000_0110: peripheral_read_data = {8'd0, shadow_bg};
      32'h1000_0114: peripheral_read_data = {31'd0, pending};
      32'h1000_0118: peripheral_read_data = frame_count;
      32'h1000_011c: peripheral_read_data = INDEPENDENT_CLOCKS ? 32'd0 : {29'd0, buttons_pressed};
      32'h1000_0120: peripheral_read_data = !INDEPENDENT_CLOCKS && GAMEPAD_INPUT ?
          {15'd0, gamepad_connected, 4'd0, gamepad_buttons} : 32'd0;
      32'h1000_0124: peripheral_read_data = !INDEPENDENT_CLOCKS && GAMEPAD_INPUT ? 32'h49500100 : 0;
      32'h1000_0128: peripheral_read_data = !INDEPENDENT_CLOCKS && GAMEPAD_INPUT ? gamepad_sequence : 0;
      32'h1000_012c: peripheral_read_data = !INDEPENDENT_CLOCKS && GAMEPAD_INPUT ? gamepad_left : 0;
      32'h1000_0130: peripheral_read_data = !INDEPENDENT_CLOCKS && GAMEPAD_INPUT ? gamepad_right : 0;
      32'h1000_0134: peripheral_read_data = !INDEPENDENT_CLOCKS && GAMEPAD_INPUT ? gamepad_aux : 0;
      default: begin
        if (AUDIO_ENABLE && peripheral_address[31:8]==24'h100003) peripheral_read_data=audio_read_data;
        else if (!INDEPENDENT_CLOCKS && external_data) peripheral_read_data = ddr_data_payload;
        else if (!INDEPENDENT_CLOCKS && ram_selected) peripheral_read_data = ram_read_data;
        else if (!INDEPENDENT_CLOCKS && asset_selected) peripheral_read_data = asset_read_data;
        else if (!INDEPENDENT_CLOCKS && USE_C_PROGRAM && data_address < 32'h0001_8000)
          peripheral_read_data = program_read_data;
        else peripheral_read_data = framebuffer_read_data;
      end
    endcase
  end

  always @(*) begin
    if(machine_timer_selected) data_read_data=machine_timer_data;
    else if(!INDEPENDENT_CLOCKS) data_read_data=peripheral_read_data;
    else if(data_address==32'h10000250) data_read_data=cpu_cycles;
    else if(data_address==32'h1000011c) data_read_data={29'd0,buttons_pressed};
    else if(gamepad_local_selected) data_read_data=cpu_gamepad_read_data;
    else if(mmio_selected) data_read_data=mmio_reply;
    else if(external_data) data_read_data=ddr_data_payload;
    else if(ram_selected) data_read_data=ram_read_data;
    else if(asset_selected) data_read_data=asset_read_data;
    else if(USE_C_PROGRAM && data_address<32'h01000) data_read_data=program_read_data;
    else data_read_data=0;
  end

  always @(posedge peripheral_clk) begin
    if (!peripheral_reset_n) begin
      shadow_x <= 0;
      shadow_y <= 0;
      shadow_w <= 0;
      shadow_h <= 0;
      live_x <= 0;
      live_y <= 0;
      live_w <= 0;
      live_h <= 0;
      shadow_color <= 0;
      live_color <= 0;
      shadow_bg <= 24'h203040;
      live_bg <= 24'h203040;
      pending <= 0;
      frame_count <= 0;
      prev_vs <= 0;
      prev_de <= 0;
      pixel_x <= 0;
      pixel_y <= 0;
      hs <= 0;
      vs <= 0;
      de <= 0;
      rgb <= 0;
      hs_d <= 0;
      vs_d <= 0;
      de_d <= 0;
      border_d <= 0;
      fps_panel_d <= 0;
      fps_ink_d <= 0;
      fps_rgb_d <= 0;
    end else begin
      prev_vs <= vs_in;
      prev_de <= de_in;
      // Synchronous BRAM read takes one clock; delay all controls equally.
      hs_d <= hs_in;
      vs_d <= vs_in;
      de_d <= de_in;
      border_d <= pixel_x < 4 || pixel_x >= 1020 || pixel_y < 4 || pixel_y >= 596;
      fps_panel_d <= fps_panel;
      fps_ink_d <= fps_ink;
      fps_rgb_d <= fps_rgb;
      hs <= hs_d;
      vs <= vs_d;
      de <= de_d;
      if (de_in) pixel_x <= pixel_x + 1'b1;
      else pixel_x <= 0;
      if (prev_de && !de_in) pixel_y <= pixel_y + 1'b1;
      if (frame_edge) begin
        pixel_y <= 0;
        frame_count <= frame_count + 1'b1;
        if (pending) begin
          live_x <= shadow_x;
          live_y <= shadow_y;
          live_w <= shadow_w;
          live_h <= shadow_h;
          live_color <= shadow_color;
          live_bg <= shadow_bg;
          pending <= 0;
        end
      end
      // Full aligned word accesses only. Writes while pending are ignored,
      // freezing the submitted snapshot until its frame-boundary commit.
      if (peripheral_write_enable && peripheral_write_strobe == 4'b1111 && !pending) begin
        case (peripheral_address)
          32'h1000_0100: shadow_x <= peripheral_write_data[10:0];
          32'h1000_0104: shadow_y <= peripheral_write_data[9:0];
          32'h1000_0108: begin
            shadow_w <= peripheral_write_data[10:0];
            shadow_h <= peripheral_write_data[25:16];
          end
          32'h1000_010c: shadow_color <= peripheral_write_data[23:0];
          32'h1000_0110: shadow_bg <= peripheral_write_data[23:0];
          32'h1000_0114: pending <= peripheral_write_data[0];
          default: begin
          end
        endcase
      end
      if (!de_d) rgb <= 0;
      else if (fps_panel_d) rgb <= fps_rgb_d;
      else rgb <= framebuffer_rgb;
    end
  end
endmodule
