`timescale 1ns / 1ps
module button_input #(
    parameter DEBOUNCE_CYCLES = 502500  // 10 ms at 50.25 MHz
) (
    input wire clk,
    input wire reset_n,
    input wire [2:0] key_n,
    output reg [2:0] pressed
);
  (* ASYNC_REG="TRUE" *) reg [2:0] key_sync1, key_sync2;
  always @(posedge clk) begin
    if (!reset_n) begin
      key_sync1 <= 3'b111;
      key_sync2 <= 3'b111;
    end else begin
      key_sync1 <= key_n;
      key_sync2 <= key_sync1;
    end
  end
  localparam COUNT_WIDTH = (DEBOUNCE_CYCLES < 2) ? 1 : $clog2(DEBOUNCE_CYCLES);
  genvar k;
  generate
    for (k = 0; k < 3; k = k + 1) begin : filter
      reg [COUNT_WIDTH-1:0] count;
      always @(posedge clk) begin
        if (!reset_n) begin
          count <= 0;
          pressed[k] <= 0;
        end else if (!key_sync2[k] == pressed[k]) count <= 0;
        else if (count == DEBOUNCE_CYCLES - 1) begin
          pressed[k] <= !key_sync2[k];
          count <= 0;
        end else count <= count + 1'b1;
      end
    end
  endgenerate
endmodule
