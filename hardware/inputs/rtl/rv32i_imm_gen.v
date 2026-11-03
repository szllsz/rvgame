`timescale 1ns / 1ps

// RV32I立即数生成器
// 不同指令把立即数字段拆在不同位置，本模块负责重新拼接并扩展为32位。
module rv32i_imm_gen (
    input  wire [31:0] instruction,
    output reg  [31:0] immediate
);

    wire [6:0] opcode;
    assign opcode = instruction[6:0];

    always @(*) begin
        immediate = 32'd0;

        case (opcode)
            7'b0010011,  // I型算术指令，例如ADDI
            7'b0000011,  // Load指令，后续扩展时使用
            7'b1100111:  // JALR指令，后续扩展时使用
                immediate = {{20{instruction[31]}}, instruction[31:20]};

            7'b0100011:  // S型：Store立即数由高7位和低5位拼接
                immediate = {{20{instruction[31]}},
                             instruction[31:25], instruction[11:7]};

            7'b1100011:  // B型：最低位固定为0，所以分支地址天然2字节对齐
                immediate = {{19{instruction[31]}}, instruction[31],
                             instruction[7], instruction[30:25],
                             instruction[11:8], 1'b0};

            7'b0110111,  // LUI
            7'b0010111:  // AUIPC，后续扩展时使用
                immediate = {instruction[31:12], 12'd0};

            7'b1101111:  // J型：JAL跳转立即数
                immediate = {{11{instruction[31]}}, instruction[31],
                             instruction[19:12], instruction[20],
                             instruction[30:21], 1'b0};

            default:
                immediate = 32'd0;
        endcase
    end

endmodule
