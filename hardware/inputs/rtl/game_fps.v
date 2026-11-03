`timescale 1ns / 1ps
// Completed front events in a rolling one-second window, updated every 250 ms.
module game_fps #(
    parameter CLOCK_HZ = 105000000  // divisible by four; simulation uses >= 256
) (
    input wire clk, reset_n,
    input wire presented, frame_edge,
    input wire [10:0] x,
    input wire [9:0] y,
    output wire panel,
    output reg ink,
    output reg [23:0] osd_rgb,
    output reg [31:0] present_count,
    output reg [31:0] window_frames,
    output reg [31:0] window_id,
    output reg window_valid,
    output wire [31:0] display_status
);
  localparam QUARTER = CLOCK_HZ/4;
  localparam CLOCK_BITS = $clog2(QUARTER) > 0 ? $clog2(QUARTER) : 1;
  reg [CLOCK_BITS-1:0] clocks;
  reg [31:0] current_bin,bin0,bin1,bin2;
  reg [2:0] quarters;
  wire [31:0] complete_bin=current_bin+{31'd0,presented};
  wire [31:0] total=complete_bin+bin0+bin1+bin2;
  // Serial decimal conversion keeps division and a large combinational font
  // converter out of the pixel path. Raw counter deliberately wraps modulo 2^32.
  reg [31:0] convert;
  reg [39:0] bcd,adjusted;
  reg [5:0] bits_left;
  reg [11:0] sampled_digits,display_digits;
  reg sampled_overflow,display_overflow,sampled_valid,display_valid;
  assign display_status={18'd0,display_overflow,display_valid,display_digits};
  integer digit;
  always @(*) begin
    adjusted=bcd;
    for(digit=0;digit<10;digit=digit+1)
      if(bcd[digit*4 +: 4]>=5) adjusted[digit*4 +: 4]=bcd[digit*4 +: 4]+4'd3;
  end
  always @(posedge clk) begin
    if (!reset_n) begin
      clocks<=0;current_bin<=0;bin0<=0;bin1<=0;bin2<=0;quarters<=0;
      present_count<=0;window_frames<=0;window_id<=0;window_valid<=0;
      convert<=0;bcd<=0;bits_left<=0;sampled_digits<=0;display_digits<=0;
      sampled_overflow<=0;display_overflow<=0;sampled_valid<=0;display_valid<=0;
    end else begin
      if(presented) present_count<=present_count+1'b1;
      if (clocks == QUARTER-1) begin
        clocks<=0;current_bin<=0;bin0<=complete_bin;bin1<=bin0;bin2<=bin1;
        if(quarters<4) quarters<=quarters+1'b1;
        if(quarters>=3) begin
          window_frames<=total;window_id<=window_id+1'b1;window_valid<=1;
          convert<=total;bcd<=0;bits_left<=32;
        end
      end else begin
        clocks<=clocks+1'b1;current_bin<=complete_bin;
      end
      if(bits_left!=0) begin
        bcd<={adjusted[38:0],convert[31]};convert<={convert[30:0],1'b0};
        bits_left<=bits_left-1'b1;
      end else if(window_valid) begin
        sampled_digits<=bcd[11:0];sampled_overflow<=|bcd[39:12];sampled_valid<=1;
      end
      // 数字只在消隐更新，避免扫描到一半时文字变化。
      if (frame_edge) begin
        display_digits<=sampled_digits;display_overflow<=sampled_overflow;display_valid<=sampled_valid;
      end
    end
  end
  // Reuse the original local HDMI test's 3x5 font, enlarged four times.
  function [14:0] font;
    input [3:0] glyph;
    begin
      case(glyph)
        0: font=15'b111_101_101_101_111;
        1: font=15'b010_110_010_010_111;
        2: font=15'b111_001_111_100_111;
        3: font=15'b111_001_111_001_111;
        4: font=15'b101_101_111_001_001;
        5: font=15'b111_100_111_001_111;
        6: font=15'b111_100_111_101_111;
        7: font=15'b111_001_001_001_001;
        8: font=15'b111_101_111_101_111;
        9: font=15'b111_101_111_001_111;
        10: font=15'b111_100_110_100_100;
        11: font=15'b110_101_110_100_100;
        12: font=15'b111_100_111_001_111;
        13: font=15'b000_010_000_010_000;
        14: font=15'b000_000_111_000_000;
        default: font=0;
      endcase
    end
  endfunction
  // Compact rounded badge: larger numeric value, quiet FPS unit, no colon.
  // Geometry and palette are entirely downstream of presentation accounting.
  function [34:0] number_font;
    input [3:0] glyph;
    begin
      case(glyph)
        0: number_font=35'b01110_10001_10011_10101_11001_10001_01110;
        1: number_font=35'b00100_01100_00100_00100_00100_00100_01110;
        2: number_font=35'b01110_10001_00001_00010_00100_01000_11111;
        3: number_font=35'b11110_00001_00001_01110_00001_00001_11110;
        4: number_font=35'b00010_00110_01010_10010_11111_00010_00010;
        5: number_font=35'b11111_10000_10000_11110_00001_00001_11110;
        6: number_font=35'b01110_10000_10000_11110_10001_10001_01110;
        7: number_font=35'b11111_00001_00010_00100_01000_01000_01000;
        8: number_font=35'b01110_10001_10001_01110_10001_10001_01110;
        9: number_font=35'b01110_10001_10001_01111_00001_00001_01110;
        14:number_font=35'b00000_00000_00000_01110_00000_00000_00000;
        default:number_font=0;
      endcase
    end
  endfunction
  wire [10:0] badge_x=x-11'd806;
  wire [9:0] badge_y=y-10'd6;
  wire corner_cut=(badge_y==0 || badge_y==31) ? (badge_x<4 || badge_x>=94) :
      (badge_y==1 || badge_y==30) ? (badge_x<2 || badge_x>=96) :
      (badge_y==2 || badge_y==29) ? (badge_x<1 || badge_x>=97) : 1'b0;
  assign panel=x>=806 && x<904 && y>=6 && y<38 && !corner_cut;
  wire inner=x>=807 && x<903 && y>=7 && y<37 &&
      !((badge_y==1 || badge_y==30) && (badge_x<4 || badge_x>=94)) &&
      !((badge_y==2 || badge_y==29) && (badge_x<2 || badge_x>=96));
  reg [3:0] glyph;
  reg [14:0] bitmap;
  reg [34:0] number_bitmap;
  integer bit_index,col,row,slot,local_x;
  always @(*) begin
    glyph=15;bitmap=0;number_bitmap=0;bit_index=0;col=0;row=0;slot=0;local_x=0;
    ink=0;osd_rgb=inner ? 24'h151d29 : 24'h384356;
    if(panel && x>=814 && x<868 && y>=11 && y<32) begin
      slot=x>=850 ? 2 : x>=832 ? 1 : 0;
      local_x=x-(slot==2 ? 850 : slot==1 ? 832 : 814);
      col=local_x>=15 ? 5 : local_x>=12 ? 4 : local_x>=9 ? 3 : local_x>=6 ? 2 : local_x>=3 ? 1 : 0;
      row=y>=29 ? 6 : y>=26 ? 5 : y>=23 ? 4 : y>=20 ? 3 : y>=17 ? 2 : y>=14 ? 1 : 0;
      case(slot)
        0:glyph=!display_valid || display_overflow || display_digits[11:8]==0 ? 15 : display_digits[11:8];
        1:glyph=!display_valid || display_overflow ? 14 :
            (display_digits[11:4]==0 ? 15 : display_digits[7:4]);
        2:glyph=!display_valid || display_overflow ? 14 : display_digits[3:0];
        default:glyph=15;
      endcase
      number_bitmap=number_font(glyph);bit_index=34-row*5-col;
      if(col<5) ink=number_bitmap[bit_index];
      if(ink) osd_rgb=24'hf3f6fa;
    end else if(panel && x>=874 && x<898 && y>=17 && y<27) begin
      local_x=x-874;slot=local_x/8;col=(local_x%8)/2;row=(y-17)/2;
      case(slot) 0:glyph=10;1:glyph=11;2:glyph=12;default:glyph=15;endcase
      bitmap=font(glyph);bit_index=14-row*3-col;
      if(col<3) ink=bitmap[bit_index];
      if(ink) osd_rgb=24'h93a4ba;
    end
  end
endmodule
