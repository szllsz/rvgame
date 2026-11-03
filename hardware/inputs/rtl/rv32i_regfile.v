`timescale 1ns / 1ps

// RV32I整数寄存器堆：共有32个32位寄存器
// x0按照RISC-V规范恒为0，即使指令尝试写x0，也会在这里直接忽略。
module rv32i_regfile (
    input  wire        clk,
    input  wire        reset_n,
    input  wire [4:0]  read_addr1,
    input  wire [4:0]  read_addr2,
    output wire [31:0] read_data1,
    output wire [31:0] read_data2,
    input  wire        write_enable,
    input  wire [4:0]  write_addr,
    input  wire [31:0] write_data
);

    reg [31:0] registers [0:31];
    integer i;

    // 两个读端口采用组合读取，译码阶段可以同时取得rs1和rs2。
    assign read_data1 = (read_addr1 == 5'd0) ? 32'd0 : registers[read_addr1];
    assign read_data2 = (read_addr2 == 5'd0) ? 32'd0 : registers[read_addr2];

    // 当前教学版本在复位时清零寄存器，便于仿真观察和排查问题。
    always @(posedge clk or negedge reset_n) begin
        if (reset_n == 1'b0) begin
            for (i = 0; i < 32; i = i + 1)
                registers[i] <= 32'd0;
        end else if ((write_enable == 1'b1) && (write_addr != 5'd0)) begin
            registers[write_addr] <= write_data;
        end
    end

endmodule
