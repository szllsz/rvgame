`timescale 1ns/1ps
// 48 KiB游戏素材：PS仅在CPU保持复位时写，RV32I同步只读。
// 参考Project F bram_sdp的同步读写推断模式；未复制其实现。
module game_asset_ram (
    input wire clk,
    input wire [31:0] read_address,
    output reg [31:0] read_data,
    input wire load_write,
    input wire [31:0] load_address,
    input wire [31:0] load_data
);
  // 三个16 KiB银行，避免48 KiB数组按64 KiB分配BRAM。
  (* ram_style = "block" *) reg [31:0] bank0 [0:4095];
  (* ram_style = "block" *) reg [31:0] bank1 [0:4095];
  (* ram_style = "block" *) reg [31:0] bank2 [0:4095];
  reg [31:0] data0, data1, data2;
  reg [1:0] read_bank;
  reg read_valid;
  wire write_valid = load_write && load_address >= 32'h30000000 &&
      load_address < 32'h3000c000 && load_address[1:0] == 0;
  always @(posedge clk) begin
    data0 <= bank0[read_address[13:2]];
    data1 <= bank1[read_address[13:2]];
    data2 <= bank2[read_address[13:2]];
    read_bank <= read_address[15:14];
    read_valid <= read_address >= 32'h30000000 && read_address < 32'h3000c000;
    if (write_valid) begin
      case (load_address[15:14])
        0: bank0[load_address[13:2]] <= load_data;
        1: bank1[load_address[13:2]] <= load_data;
        2: bank2[load_address[13:2]] <= load_data;
        default: begin end
      endcase
    end
  end
  always @(*) begin
    read_data = 0;
    if (read_valid) begin
      case (read_bank)
        0: read_data = data0;
        1: read_data = data1;
        2: read_data = data2;
        default: read_data = 0;
      endcase
    end
  end
endmodule
