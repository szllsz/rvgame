// PS控制寄存器：AXI-Lite单笔缓冲，AW与W独立接收，响应等待时保持数据。
// 协议参考 ZipCPU/wb2axip easyaxil (Apache-2.0)，本模块未复制上游代码。
module ps_control #(
    parameter VIDEO_CDC = 0,
    parameter PAD_TIMEOUT_CYCLES = 125000000, // 1秒；有效状态写入作为输入服务心跳
    parameter FRAME_WIDTH = 256,
    parameter FRAME_HEIGHT = 240
) (
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi, ASSOCIATED_RESET s_axi_aresetn, FREQ_HZ 105000000" *)
    input wire s_axi_aclk,
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *) input wire s_axi_aresetn,
    input wire [11:0] s_axi_awaddr,
    input wire [2:0] s_axi_awprot,
    input wire s_axi_awvalid,
    output wire s_axi_awready,
    input wire [31:0] s_axi_wdata,
    input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid,
    output wire s_axi_wready,
    output reg [1:0] s_axi_bresp,
    output reg s_axi_bvalid = 0,
    input wire s_axi_bready,
    input wire [11:0] s_axi_araddr,
    input wire [2:0] s_axi_arprot,
    input wire s_axi_arvalid,
    output wire s_axi_arready,
    output reg [31:0] s_axi_rdata,
    output reg [1:0] s_axi_rresp,
    output reg s_axi_rvalid = 0,
    input wire s_axi_rready,
    input wire cpu_quiescent,
    output wire video_request,output reg video_write,
    output reg [11:0] video_address,output reg [31:0] video_write_data,
    output reg [3:0] video_write_strobe,
    input wire video_ready,input wire [31:0] video_read_data,input wire [1:0] video_response,
    input wire [31:0] frame_count,
    input wire [31:0] cpu_pc,
    input wire [2:0] board_buttons,
    output reg [2:0] host_buttons = 0,
    output reg host_enable = 0,
    input wire hdmi_connected,
    input wire [31:0] game_position,
    output reg [11:0] gamepad_buttons = 0,
    output reg gamepad_connected = 0,
    output reg [31:0] gamepad_left = 0, gamepad_right = 0, gamepad_aux = 0,
    output reg [31:0] gamepad_sequence = 0,
    output reg cpu_hold = 0,
    output reg [31:0] load_address = 0,
    output reg [31:0] load_data,
    output reg load_write = 0,
    input wire [31:0] load_read_data,
    output reg [31:0] ddr_base = 0,
    output reg [31:0] ddr_size = 0,
    output reg ddr_enable = 0,
    output reg ddr_flush = 0,
    input wire ddr_busy,
    input wire ddr_fault,
    output reg [31:0] frame_base = 0,
    output reg frame_enable = 0,
    input wire frame_busy,
    input wire frame_fault,
    input wire [31:0] frame_underflows,
    input wire [31:0] frame_copies
);
  reg aw_pending = 0, w_pending = 0;
  reg [11:0] write_address;
  reg [31:0] write_data;
  reg [3:0] write_strobe;
  reg [31:0] scratch, cycles, write_count;
  reg [31:0] load_count;
  // CPU-clock snapshot v1: three staging words, then one atomic commit.
  // Legacy 0x40 commits invalidate extended axes. No extra input writer.
  reg [31:0] pad_left_stage=0,pad_right_stage=0,pad_aux_stage=0;
  reg [2:0] pad_staged=0;
  localparam PAD_TIMEOUT_BITS=PAD_TIMEOUT_CYCLES>1 ? $clog2(PAD_TIMEOUT_CYCLES) : 1;
  reg [PAD_TIMEOUT_BITS-1:0] pad_timeout = 0;
  reg [1:0] read_pending = 0;
  reg video_pending=0;
  reg recover_active=0,reset_seen=0,recovery_final_hold=0;
  reg [2:0] recover_step=0;
  assign video_request=VIDEO_CDC && video_pending && (video_address!=12'h028 || cpu_quiescent);
  function video_write_selected;
    input [11:0] a;
    begin
      case(a) 12'h028,12'h054,12'h058,12'h064,12'h06c,12'h070,12'h0b0:video_write_selected=1;
        default:video_write_selected=0;endcase
    end
  endfunction
  function video_read_selected;
    input [11:0] a;
    begin
      case(a) 12'h010,12'h048,12'h06c,12'h070,12'h074,12'h078,12'h07c,
        12'h080,12'h084,12'h088,12'h08c,12'h0b4,12'h0b8,12'h0bc,
        12'h0c0,12'h0c4,12'h0c8,12'h0cc,12'h0d0,12'h0d4,12'h0d8,12'h0dc:video_read_selected=1;
        default:video_read_selected=0;endcase
    end
  endfunction
  wire video_write_allowed=write_strobe==15 && read_pending==0 &&
      (write_address==12'h0b0 ? !cpu_hold :
       write_address==12'h028 ? (write_data<=1 && (write_data[0] || !ddr_enable || !ddr_busy)) :
       write_address==12'h054 ? (cpu_hold && !ddr_busy && write_data[5:0]==0 && write_data<32'h40000000) :
       write_address==12'h058 ? (cpu_hold && !ddr_busy && write_data<=1 &&
           (!write_data[0] || (ddr_size>=64 && ddr_base<=32'h40000000-ddr_size))) :
       write_address==12'h064 ? (cpu_hold && !ddr_busy && write_data[5:0]==0 && write_data>=64 && write_data<=32'h01000000) :
       write_address==12'h06c ? (cpu_hold && write_data[5:0]==0 && write_data<=32'h3ff00000) :
       (cpu_hold && write_data<=1));
  assign s_axi_awready = s_axi_aresetn && !aw_pending && !s_axi_bvalid && !video_pending && !recover_active;
  assign s_axi_wready = s_axi_aresetn && !w_pending && !s_axi_bvalid && !video_pending && !recover_active;
  assign s_axi_arready = s_axi_aresetn && !s_axi_rvalid && read_pending==0 && !video_pending && !recover_active && !(aw_pending && w_pending && !s_axi_bvalid);
  integer lane;
  always @(posedge s_axi_aclk) begin
    if (!s_axi_aresetn) begin
      if(VIDEO_CDC) begin
        if(!reset_seen) recovery_final_hold<=cpu_hold || ddr_enable;
        reset_seen<=1;recover_active<=1;recover_step<=0;
      end
      // 保留cpu_hold：加载中复位控制口不能意外启动部分程序；上电初值为0。
      if (ddr_enable || VIDEO_CDC) cpu_hold <= 1; // 控制/PS复位后先暂停DDR程序，需重新确认缓冲区有效。
      load_write <= 0;
      frame_enable <= 0;
      ddr_flush <= 0;
      load_address <= 0;
      load_count <= 0;
      read_pending <= 0;video_pending<=0;video_write<=0;video_address<=0;video_write_data<=0;video_write_strobe<=0;
      aw_pending <= 0;
      w_pending <= 0;
      s_axi_bvalid <= 0;
      s_axi_bresp <= 0;
      s_axi_rvalid <= 0;
      s_axi_rresp <= 0;
      s_axi_rdata <= 0;
      scratch <= 0;
      cycles <= 0;
      write_count <= 0;
      host_buttons <= 0;
      host_enable <= 0;
      gamepad_buttons <= 0;
      gamepad_connected <= 0;
      gamepad_left<=0;gamepad_right<=0;gamepad_aux<=0;gamepad_sequence<=0;
      pad_left_stage<=0;pad_right_stage<=0;pad_aux_stage<=0;
      pad_staged<=0;
      pad_timeout <= 0;
    end else begin
      reset_seen<=0;
      if(VIDEO_CDC && recover_active && !video_pending) begin
        video_write<=1;video_write_strobe<=15;video_pending<=1;
        case(recover_step)
          0:begin video_address<=12'h028;video_write_data<=1;end
          1:begin video_address<=12'h054;video_write_data<=ddr_base;end
          2:if(ddr_size>=64) begin video_address<=12'h064;video_write_data<=ddr_size;end
            else begin video_pending<=0;recover_step<=3;end
          3:begin video_address<=12'h058;video_write_data<={31'd0,ddr_enable};end
          4:begin video_address<=12'h06c;video_write_data<=frame_base;end
          5:begin video_address<=12'h028;video_write_data<={31'd0,recovery_final_hold};end
          default:begin video_pending<=0;recover_step<=0;end
        endcase
      end
      load_write <= 0;
      ddr_flush <= 0;
      cycles <= cycles + 1'b1;
      if (gamepad_connected) begin
        if (pad_timeout == PAD_TIMEOUT_CYCLES-1) begin
          gamepad_connected <= 0;
          gamepad_buttons <= 0;
          gamepad_left<=0;gamepad_right<=0;gamepad_aux<=0;
          gamepad_sequence<=gamepad_sequence+1'b1;
          pad_staged<=0;
          pad_timeout <= 0;
        end else pad_timeout <= pad_timeout + 1'b1;
      end
      if (s_axi_awvalid && s_axi_awready) begin
        write_address <= s_axi_awaddr;
        aw_pending <= 1;
      end
      if (s_axi_wvalid && s_axi_wready) begin
        write_data <= s_axi_wdata;
        write_strobe <= s_axi_wstrb;
        w_pending <= 1;
      end
      if (s_axi_bvalid && s_axi_bready) s_axi_bvalid <= 0;
      if (aw_pending && w_pending && !s_axi_bvalid && !video_pending) begin
        aw_pending <= 0;
        w_pending <= 0;
        s_axi_bvalid <= 1;
        s_axi_bresp <= 0;
        if(VIDEO_CDC && video_write_selected(write_address)) begin
          if(video_write_allowed) begin
            video_pending<=1;video_write<=1;video_address<=write_address;
            video_write_data<=write_data;video_write_strobe<=write_strobe;s_axi_bvalid<=0;
            if(write_address==12'h028 && write_data[0]) cpu_hold<=1;
          end else s_axi_bresp<=2'b10;
        end else begin
        case (write_address)
          12'h008: begin
            for (lane = 0; lane < 4; lane = lane + 1)
              if (write_strobe[lane]) scratch[lane*8 +: 8] <= write_data[lane*8 +: 8];
            write_count <= write_count + 1'b1;
          end
          12'h00c: begin
            if (write_strobe[0]) host_buttons <= write_data[2:0];
            if (write_strobe[1]) host_enable <= write_data[8];
            write_count <= write_count + 1'b1;
          end
          12'h040: begin
            // 原子快照：bit16连接，低12位按键；断开时必须清空。
            if (write_strobe==15 && (write_data & 32'hfffe_f000)==0) begin
              gamepad_connected <= write_data[16];
              gamepad_buttons <= write_data[16] ? write_data[11:0] : 12'd0;
              gamepad_left<=0;gamepad_right<=0;gamepad_aux<=0;
              gamepad_sequence<=gamepad_sequence+1'b1;
              pad_staged<=0;
              pad_timeout <= 0;
            end else s_axi_bresp <= 2'b10;
          end
          12'h094,12'h098,12'h09c: begin
            if(write_strobe==15 && (write_address!=12'h09c || write_data[31:28]==0)) begin
              case(write_address)
                12'h094:begin pad_left_stage<=write_data;pad_staged[0]<=1;end
                12'h098:begin pad_right_stage<=write_data;pad_staged[1]<=1;end
                default:begin pad_aux_stage<=write_data;pad_staged[2]<=1;end
              endcase
            end else s_axi_bresp<=2'b10;
          end
          12'h0a0: begin
            if(write_strobe==15 && (write_data & 32'hfffe_f000)==0 &&
                (!write_data[16] || &pad_staged)) begin
              gamepad_connected<=write_data[16];
              gamepad_buttons<=write_data[16] ? write_data[11:0] : 12'd0;
              gamepad_left<=write_data[16] ? pad_left_stage : 0;
              gamepad_right<=write_data[16] ? pad_right_stage : 0;
              gamepad_aux<=write_data[16] ? (pad_aux_stage | 32'h80000000) : 0;
              gamepad_sequence<=gamepad_sequence+1'b1;pad_timeout<=0;
              pad_staged<=0;
            end else s_axi_bresp<=2'b10;
          end
          12'h028: begin
            if (write_strobe==15 && write_data<=1 && read_pending==0 &&
                (write_data[0] || !ddr_enable || !ddr_busy)) cpu_hold <= write_data[0];
            else s_axi_bresp <= 2'b10;
          end
          12'h02c: begin
            if (cpu_hold && write_strobe==15 && write_data[1:0]==0 && read_pending==0 &&
                (write_data<32'h01000 || (write_data>=32'h30000000 && write_data<32'h3000c000)))
              load_address <= write_data;
            else s_axi_bresp <= 2'b10;
          end
          12'h030: begin
            if (cpu_hold && write_strobe==15 && read_pending==0) begin
              load_data <= write_data;
              load_write <= 1;
              load_count <= load_count + 1'b1;
            end else s_axi_bresp <= 2'b10;
          end
          12'h054: begin
            if (cpu_hold && !ddr_busy && write_strobe==15 && write_data[5:0]==0 && write_data<32'h40000000)
              ddr_base <= write_data;
            else s_axi_bresp <= 2'b10;
          end
          12'h058: begin
            if (cpu_hold && !ddr_busy && write_strobe==15 && write_data<=1 &&
                (!write_data[0] || (ddr_size>=64 && ddr_base<=32'h40000000-ddr_size)))
              ddr_enable <= write_data[0];
            else s_axi_bresp <= 2'b10;
          end
          12'h05c: begin
            if (cpu_hold && !ddr_busy && write_strobe==15 && write_data==1) ddr_flush <= 1;
            else s_axi_bresp <= 2'b10;
          end
          12'h064: begin
            if (cpu_hold && !ddr_busy && write_strobe==15 && write_data[5:0]==0 &&
                write_data>=64 && write_data<=32'h01000000)
              ddr_size <= write_data;
            else s_axi_bresp <= 2'b10;
          end
          12'h06c: begin
            if (cpu_hold && !frame_enable && !frame_busy && write_strobe==15 &&
                write_data[5:0]==0 && write_data<=32'h3ff00000) frame_base<=write_data;
            else s_axi_bresp<=2'b10;
          end
          12'h070: begin
            if (cpu_hold && write_strobe==15 && write_data<=1 && (!write_data[0] || !frame_busy))
                frame_enable<=write_data[0];
            else s_axi_bresp<=2'b10;
          end
          default: s_axi_bresp <= 2'b10;  // SLVERR：未映射/未对齐/只读地址
        endcase
      end
      end
      if(VIDEO_CDC && video_pending && video_request && video_ready) begin
        video_pending<=0;
        if(recover_active) begin
          // Keep CPU held and retry a rejected restore step; never acknowledge uncommitted state.
          if(video_response==0) begin
            if(recover_step==5) begin recover_active<=0;cpu_hold<=recovery_final_hold;end
            else recover_step<=recover_step+1'b1;
          end
        end else if(video_write) begin
          s_axi_bvalid<=1;s_axi_bresp<=video_response;
          if(video_response==0) case(video_address)
            12'h028:cpu_hold<=video_write_data[0];
            12'h054:ddr_base<=video_write_data;
            12'h058:ddr_enable<=video_write_data[0];
            12'h064:ddr_size<=video_write_data;
            12'h06c:frame_base<=video_write_data;
            12'h070:frame_enable<=video_write_data[0];
            default:begin end
          endcase
        end else begin s_axi_rvalid<=1;s_axi_rdata<=video_read_data;s_axi_rresp<=video_response;end
      end
      if (s_axi_rvalid && s_axi_rready) s_axi_rvalid <= 0;
      if (read_pending!=0) begin
        read_pending <= read_pending - 1'b1;
        if (read_pending==1) begin
          s_axi_rdata <= load_read_data;
          s_axi_rvalid <= 1;
        end
      end
      if (s_axi_arvalid && s_axi_arready) begin
        s_axi_rvalid <= 1;
        s_axi_rresp <= 0;
        if(VIDEO_CDC && video_read_selected(s_axi_araddr)) begin
          video_pending<=1;video_write<=0;video_address<=s_axi_araddr;
          video_write_data<=0;video_write_strobe<=0;s_axi_rvalid<=0;
        end else begin
        case (s_axi_araddr)
          12'h000: s_axi_rdata <= 32'h5247414d; // RGAM
          12'h004: s_axi_rdata <= 32'h00010001;
          12'h008: s_axi_rdata <= scratch;
          12'h00c: s_axi_rdata <= {23'd0, host_enable, 5'd0, host_buttons};
          12'h010: s_axi_rdata <= frame_count;
          12'h014: s_axi_rdata <= cpu_pc;
          12'h018: s_axi_rdata <= (FRAME_HEIGHT<<16)|FRAME_WIDTH;
          12'h01c: s_axi_rdata <= {29'd0, board_buttons};
          12'h020: s_axi_rdata <= cycles;
          12'h024: s_axi_rdata <= write_count;
          12'h028: s_axi_rdata <= {31'd0, cpu_hold};
          12'h02c: s_axi_rdata <= load_address;
          12'h030: begin
            if (cpu_hold) begin s_axi_rvalid <= 0; read_pending <= 2; end
            else begin s_axi_rdata <= 0; s_axi_rresp <= 2'b10; end
          end
          12'h034: s_axi_rdata <= load_count;
          12'h038: s_axi_rdata <= 32'h01000;
          12'h040: s_axi_rdata <= {15'd0, gamepad_connected, 4'd0, gamepad_buttons};
          12'h044: s_axi_rdata <= {30'd0, gamepad_connected, hdmi_connected};
          12'h090: s_axi_rdata <= 32'h49500100;
          12'h094: s_axi_rdata <= gamepad_left;
          12'h098: s_axi_rdata <= gamepad_right;
          12'h09c: s_axi_rdata <= gamepad_aux;
          12'h0a0: s_axi_rdata <= gamepad_sequence;
          12'h048: s_axi_rdata <= game_position;
          12'h04c: s_axi_rdata <= 32'hc000; // 独立素材空间48 KiB
          12'h050: s_axi_rdata <= 32'h30000000; // RV32I素材只读基地址
          12'h054: s_axi_rdata <= ddr_base;
          12'h058: s_axi_rdata <= {31'd0,ddr_enable};
          12'h05c: s_axi_rdata <= 0; // flush为写一触发
          12'h060: s_axi_rdata <= {30'd0,ddr_busy,ddr_fault};
          12'h064: s_axi_rdata <= ddr_size;
          12'h068: s_axi_rdata <= 32'h01000000; // 最大DDR只读窗口16 MiB
          12'h06c: s_axi_rdata <= frame_base;
          12'h070: s_axi_rdata <= {31'd0,frame_enable};
          12'h074: s_axi_rdata <= {30'd0,frame_busy,frame_fault};
          12'h078: s_axi_rdata <= frame_underflows;
          12'h07c: s_axi_rdata <= frame_copies;
          default: begin s_axi_rdata <= 0; s_axi_rresp <= 2'b10; end
        endcase
        end
      end
    end
  end
endmodule
