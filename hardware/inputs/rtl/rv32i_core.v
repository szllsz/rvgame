`timescale 1ns / 1ps

// 最小多周期RV32I处理器
//
// 一条指令不会在一个时钟内做完，而是依次经过：
//   1. FETCH     取指
//   2. DECODE    译码并读取寄存器
//   3. EXECUTE   运算、比较或计算访存地址
//   4. MEMORY    访问存储器/外设（只有访存指令需要）
//   5. WRITEBACK 写回寄存器（只有产生寄存器结果的指令需要）
//
// 支持题目要求的全部37条RV32I指令（不含FENCE、ECALL和EBREAK）。
// 接口特意保持简单，后续补齐37条RV32I指令时无需推翻顶层结构。
module rv32i_core #(
    parameter SYNC_INSTRUCTION = 0  // 1为地址寄存器+同步BRAM增加两拍取指等待；默认保留组合ROM
) (
    input  wire        clk,
    input  wire        reset_n,

    // 指令存储器接口：地址由PC给出，当前ROM为组合读取
    output wire [31:0] instruction_address,
    input  wire [31:0] instruction_data,

    // 数据存储器/MMIO接口
    output wire [31:0] data_address,
    output wire [31:0] data_write_data,
    output wire [3:0]  data_write_strobe,
    output wire        data_write_enable,
    input  wire [31:0] data_read_data,

    // 预留给ILA的调试信号
    output wire [31:0] debug_pc,
    output wire [31:0] debug_instruction,
    output wire [2:0]  debug_state,
    output wire        debug_reg_write,
    output wire [4:0]  debug_reg_write_addr,
    output wire [31:0] debug_reg_write_data
);

    localparam STATE_FETCH     = 3'd0;
    localparam STATE_DECODE    = 3'd1;
    localparam STATE_EXECUTE   = 3'd2;
    localparam STATE_MEMORY    = 3'd3;
    localparam STATE_WRITEBACK = 3'd4;
    localparam STATE_MEMORY_WAIT = 3'd5;
    localparam STATE_FETCH_WAIT = 3'd6;
    localparam STATE_FETCH_CAPTURE = 3'd7;

    localparam OPCODE_LUI      = 7'b0110111;
    localparam OPCODE_AUIPC    = 7'b0010111;
    localparam OPCODE_OP_IMM   = 7'b0010011;
    localparam OPCODE_OP       = 7'b0110011;
    localparam OPCODE_LOAD     = 7'b0000011;
    localparam OPCODE_STORE    = 7'b0100011;
    localparam OPCODE_BRANCH   = 7'b1100011;
    localparam OPCODE_JAL      = 7'b1101111;
    localparam OPCODE_JALR     = 7'b1100111;

    // 与rv32i_alu中的运算编号保持一致。
    localparam ALU_ADD  = 4'd0;
    localparam ALU_SUB  = 4'd1;
    localparam ALU_AND  = 4'd2;
    localparam ALU_OR   = 4'd3;
    localparam ALU_XOR  = 4'd4;
    localparam ALU_SLT  = 4'd5;
    localparam ALU_SLTU = 4'd6;
    localparam ALU_SLL  = 4'd7;
    localparam ALU_SRL  = 4'd8;
    localparam ALU_SRA  = 4'd9;

    reg [2:0]  state;
    reg [31:0] pc;
    reg [31:0] instruction_reg;
    reg [31:0] instruction_pc_reg;
    reg [31:0] operand_a_reg;
    reg [31:0] operand_b_reg;
    reg [31:0] execute_result_reg;
    reg [31:0] memory_address_reg;
    reg [31:0] memory_write_data_reg;
    reg [3:0]  memory_write_strobe_reg;
    reg        memory_read_reg;
    reg        writeback_enable_reg;

    wire [6:0] opcode;
    wire [2:0] funct3;
    wire [6:0] funct7;
    wire [4:0] rs1_address;
    wire [4:0] rs2_address;
    wire [4:0] rd_address;

    assign opcode     = instruction_reg[6:0];
    assign funct3     = instruction_reg[14:12];
    assign funct7     = instruction_reg[31:25];
    assign rs1_address = instruction_reg[19:15];
    assign rs2_address = instruction_reg[24:20];
    assign rd_address  = instruction_reg[11:7];

    wire [31:0] immediate;

    rv32i_imm_gen u_rv32i_imm_gen (
        .instruction(instruction_reg),
        .immediate  (immediate)
    );

    wire [31:0] register_read_data1;
    wire [31:0] register_read_data2;
    wire        register_write_enable;

    // 只有WRITEBACK阶段才真正写寄存器，防止一条指令重复写多次。
    assign register_write_enable = (state == STATE_WRITEBACK) &&
                                   (writeback_enable_reg == 1'b1) &&
                                   (rd_address != 5'd0);

    rv32i_regfile u_rv32i_regfile (
        .clk         (clk),
        .reset_n     (reset_n),
        .read_addr1  (rs1_address),
        .read_addr2  (rs2_address),
        .read_data1  (register_read_data1),
        .read_data2  (register_read_data2),
        .write_enable(register_write_enable),
        .write_addr  (rd_address),
        .write_data  (execute_result_reg)
    );

    reg  [31:0] alu_operand_a;
    reg  [31:0] alu_operand_b;
    reg  [3:0]  alu_operation;
    reg         r_type_valid;
    reg         i_type_valid;
    wire [31:0] alu_result;

    // R型和I型指令都根据funct3选择ALU运算。
    // 移位指令还必须检查funct7，防止未定义编码误写寄存器。
    always @(*) begin
        alu_operand_a = operand_a_reg;
        alu_operand_b = operand_b_reg;
        alu_operation = ALU_ADD;
        r_type_valid  = 1'b0;
        i_type_valid  = 1'b0;

        case (opcode)
            OPCODE_LUI: begin
                alu_operand_a = 32'd0;
                alu_operand_b = immediate;
            end

            OPCODE_AUIPC: begin
                // AUIPC使用“本条指令的PC”，不能使用取指阶段已经加4后的pc。
                alu_operand_a = instruction_pc_reg;
                alu_operand_b = immediate;
            end

            OPCODE_LOAD,
            OPCODE_STORE,
            OPCODE_JALR: begin
                alu_operand_b = immediate;
            end

            OPCODE_OP_IMM: begin
                // 普通I型指令使用符号扩展后的12位立即数；
                // 移位指令的ALU只读取operand_b[4:0]，也就是shamt。
                alu_operand_b = immediate;

                case (funct3)
                    3'b000: begin  // ADDI
                        alu_operation = ALU_ADD;
                        i_type_valid  = 1'b1;
                    end

                    3'b010: begin  // SLTI（有符号比较）
                        alu_operation = ALU_SLT;
                        i_type_valid  = 1'b1;
                    end

                    3'b011: begin  // SLTIU（无符号比较）
                        alu_operation = ALU_SLTU;
                        i_type_valid  = 1'b1;
                    end

                    3'b100: begin  // XORI
                        alu_operation = ALU_XOR;
                        i_type_valid  = 1'b1;
                    end

                    3'b110: begin  // ORI
                        alu_operation = ALU_OR;
                        i_type_valid  = 1'b1;
                    end

                    3'b111: begin  // ANDI
                        alu_operation = ALU_AND;
                        i_type_valid  = 1'b1;
                    end

                    3'b001: begin  // SLLI只允许funct7=0000000
                        if (funct7 == 7'b0000000) begin
                            alu_operation = ALU_SLL;
                            i_type_valid  = 1'b1;
                        end
                    end

                    3'b101: begin
                        if (funct7 == 7'b0000000) begin
                            alu_operation = ALU_SRL;  // SRLI
                            i_type_valid  = 1'b1;
                        end else if (funct7 == 7'b0100000) begin
                            alu_operation = ALU_SRA;  // SRAI
                            i_type_valid  = 1'b1;
                        end
                    end

                    default: begin
                        alu_operation = ALU_ADD;
                        i_type_valid  = 1'b0;
                    end
                endcase
            end

            OPCODE_OP: begin
                case (funct3)
                    3'b000: begin
                        if (funct7 == 7'b0000000) begin
                            alu_operation = ALU_ADD;
                            r_type_valid  = 1'b1;
                        end else if (funct7 == 7'b0100000) begin
                            alu_operation = ALU_SUB;
                            r_type_valid  = 1'b1;
                        end
                    end

                    3'b001: begin
                        if (funct7 == 7'b0000000) begin
                            alu_operation = ALU_SLL;
                            r_type_valid  = 1'b1;
                        end
                    end

                    3'b010: begin
                        if (funct7 == 7'b0000000) begin
                            alu_operation = ALU_SLT;
                            r_type_valid  = 1'b1;
                        end
                    end

                    3'b011: begin
                        if (funct7 == 7'b0000000) begin
                            alu_operation = ALU_SLTU;
                            r_type_valid  = 1'b1;
                        end
                    end

                    3'b100: begin
                        if (funct7 == 7'b0000000) begin
                            alu_operation = ALU_XOR;
                            r_type_valid  = 1'b1;
                        end
                    end

                    3'b101: begin
                        if (funct7 == 7'b0000000) begin
                            alu_operation = ALU_SRL;
                            r_type_valid  = 1'b1;
                        end else if (funct7 == 7'b0100000) begin
                            alu_operation = ALU_SRA;
                            r_type_valid  = 1'b1;
                        end
                    end

                    3'b110: begin
                        if (funct7 == 7'b0000000) begin
                            alu_operation = ALU_OR;
                            r_type_valid  = 1'b1;
                        end
                    end

                    3'b111: begin
                        if (funct7 == 7'b0000000) begin
                            alu_operation = ALU_AND;
                            r_type_valid  = 1'b1;
                        end
                    end

                    default: begin
                        alu_operation = ALU_ADD;
                        r_type_valid  = 1'b0;
                    end
                endcase
            end

            default: begin
                alu_operand_a = operand_a_reg;
                alu_operand_b = operand_b_reg;
            end
        endcase
    end

    rv32i_alu u_rv32i_alu (
        .operand_a(alu_operand_a),
        .operand_b(alu_operand_b),
        .operation(alu_operation),
        .result   (alu_result)
    );

    reg branch_valid;
    reg branch_taken;

    // 六条B型分支指令共用这一组比较逻辑。
    // BLT/BGE按有符号数比较，BLTU/BGEU按无符号数比较；两者不能混用。
    always @(*) begin
        branch_valid = 1'b1;
        branch_taken = 1'b0;

        case (funct3)
            3'b000: branch_taken = (operand_a_reg == operand_b_reg);                  // BEQ
            3'b001: branch_taken = (operand_a_reg != operand_b_reg);                  // BNE
            3'b100: branch_taken = ($signed(operand_a_reg) < $signed(operand_b_reg));  // BLT
            3'b101: branch_taken = ($signed(operand_a_reg) >= $signed(operand_b_reg)); // BGE
            3'b110: branch_taken = (operand_a_reg < operand_b_reg);                    // BLTU
            3'b111: branch_taken = (operand_a_reg >= operand_b_reg);                   // BGEU
            default: begin
                branch_valid = 1'b0;
                branch_taken = 1'b0;
            end
        endcase
    end

    assign instruction_address = pc;
    assign data_address         = memory_address_reg;
    assign data_write_data      = memory_write_data_reg;
    assign data_write_strobe    = (state == STATE_MEMORY) ?
                                  memory_write_strobe_reg : 4'b0000;
    assign data_write_enable    = |data_write_strobe;

    // debug_pc使用“当前指令被取出时的PC”，这样ILA里的PC和instruction一一对应。
    // 如果直接观察pc寄存器，在译码/执行阶段看到的会是已经加4后的下一条地址。
    assign debug_pc             = instruction_pc_reg;
    assign debug_instruction    = instruction_reg;
    assign debug_state          = state;
    assign debug_reg_write      = register_write_enable;
    assign debug_reg_write_addr = rd_address;
    assign debug_reg_write_data = execute_result_reg;

    // RAM按32位字返回数据，Load指令再根据地址低两位选择字节或半字。
    wire [7:0] selected_load_byte;
    wire [15:0] selected_load_half;

    assign selected_load_byte = (memory_address_reg[1:0] == 2'b00) ? data_read_data[7:0]   :
                                (memory_address_reg[1:0] == 2'b01) ? data_read_data[15:8]  :
                                (memory_address_reg[1:0] == 2'b10) ? data_read_data[23:16] :
                                                                    data_read_data[31:24];
    assign selected_load_half = (memory_address_reg[1] == 1'b0) ?
                                data_read_data[15:0] : data_read_data[31:16];

    // 访存地址直接连接BRAM，使用同步复位避免REQP-1839异步地址警告。
    // 地址仍在EXECUTE时钟沿更新，Load/Store周期及总线接口保持不变。
    always @(posedge clk) begin
        if (reset_n == 1'b0) memory_address_reg <= 32'd0;
        else if (state == STATE_EXECUTE &&
                 ((opcode == OPCODE_LOAD &&
                   (funct3 == 3'b000 || funct3 == 3'b001 || funct3 == 3'b010 ||
                    funct3 == 3'b100 || funct3 == 3'b101)) ||
                  (opcode == OPCODE_STORE &&
                   (funct3 == 3'b000 || funct3 == 3'b001 || funct3 == 3'b010))))
            memory_address_reg <= alu_result;
    end

    always @(posedge clk or negedge reset_n) begin
        if (reset_n == 1'b0) begin
            state                 <= STATE_FETCH;
            pc                    <= 32'd0;
            instruction_reg       <= 32'h0000_0013;  // ADDI x0,x0,0，即NOP
            instruction_pc_reg    <= 32'd0;
            operand_a_reg         <= 32'd0;
            operand_b_reg         <= 32'd0;
            execute_result_reg    <= 32'd0;
            memory_write_data_reg <= 32'd0;
            memory_write_strobe_reg <= 4'b0000;
            memory_read_reg       <= 1'b0;
            writeback_enable_reg  <= 1'b0;
        end else begin
            case (state)
                STATE_FETCH: begin
                    if (SYNC_INSTRUCTION) begin
                        memory_write_strobe_reg <= 4'b0000;
                        memory_read_reg <= 1'b0;
                        writeback_enable_reg <= 1'b0;
                        state <= STATE_FETCH_WAIT;
                    end else begin
                    // 保存当前指令及其PC，同时让PC默认指向下一条指令。
                    instruction_reg      <= instruction_data;
                    instruction_pc_reg   <= pc;
                    pc                   <= pc + 32'd4;
                    memory_write_strobe_reg <= 4'b0000;
                    memory_read_reg      <= 1'b0;
                    writeback_enable_reg <= 1'b0;
                    state                <= STATE_DECODE;
                    end
                end

                STATE_FETCH_WAIT: begin
                    state <= STATE_FETCH_CAPTURE;
                end

                STATE_FETCH_CAPTURE: begin
                    instruction_reg <= instruction_data;
                    instruction_pc_reg <= pc;
                    pc <= pc + 32'd4;
                    state <= STATE_DECODE;
                end

                STATE_DECODE: begin
                    // 寄存器堆是组合读，本拍把两个源操作数锁存下来。
                    operand_a_reg <= register_read_data1;
                    operand_b_reg <= register_read_data2;
                    state         <= STATE_EXECUTE;
                end

                STATE_EXECUTE: begin
                    case (opcode)
                        OPCODE_LUI: begin
                            execute_result_reg   <= alu_result;
                            writeback_enable_reg <= 1'b1;
                            state                <= STATE_WRITEBACK;
                        end

                        OPCODE_AUIPC: begin
                            // AUIPC：rd=当前指令PC+(20位立即数左移12位)。
                            execute_result_reg   <= alu_result;
                            writeback_enable_reg <= 1'b1;
                            state                <= STATE_WRITEBACK;
                        end

                        OPCODE_OP_IMM: begin
                            // 9条I型整数运算共用ALU和寄存器写回路径。
                            if (i_type_valid == 1'b1) begin
                                execute_result_reg   <= alu_result;
                                writeback_enable_reg <= 1'b1;
                                state                <= STATE_WRITEBACK;
                            end else begin
                                state <= STATE_FETCH;
                            end
                        end

                        OPCODE_OP: begin
                            // 所有10条R型运算都共用同一条ALU写回路径。
                            if (r_type_valid == 1'b1) begin
                                execute_result_reg   <= alu_result;
                                writeback_enable_reg <= 1'b1;
                                state                <= STATE_WRITEBACK;
                            end else begin
                                state <= STATE_FETCH;
                            end
                        end

                        OPCODE_STORE: begin
                            // 字节写使能决定32位总线中哪些字节真正写入RAM。
                            // 题目不要求非对齐访问，因此软件应保证SH按2字节、SW按4字节对齐。
                            case (funct3)
                                3'b000: begin  // SB
                                    memory_write_data_reg   <= {4{operand_b_reg[7:0]}};
                                    memory_write_strobe_reg <= 4'b0001 << alu_result[1:0];
                                    state                   <= STATE_MEMORY;
                                end

                                3'b001: begin  // SH
                                    memory_write_data_reg   <= {2{operand_b_reg[15:0]}};
                                    memory_write_strobe_reg <= alu_result[1] ? 4'b1100 : 4'b0011;
                                    state                   <= STATE_MEMORY;
                                end

                                3'b010: begin  // SW
                                    memory_write_data_reg   <= operand_b_reg;
                                    memory_write_strobe_reg <= 4'b1111;
                                    state                   <= STATE_MEMORY;
                                end

                                default: begin
                                    state <= STATE_FETCH;
                                end
                            endcase
                        end

                        OPCODE_LOAD: begin
                            // LB/LH/LW/LBU/LHU先计算有效地址，再等待同步RAM返回一个32位字。
                            case (funct3)
                                3'b000,  // LB
                                3'b001,  // LH
                                3'b010,  // LW
                                3'b100,  // LBU
                                3'b101: begin  // LHU
                                    memory_read_reg    <= 1'b1;
                                    state              <= STATE_MEMORY;
                                end

                                default: begin
                                    state <= STATE_FETCH;
                                end
                            endcase
                        end

                        OPCODE_BRANCH: begin
                            // 条件成立时，目标地址=本条分支指令的PC+B型立即数。
                            // 条件不成立或funct3未定义时，继续使用取指阶段已计算的PC+4。
                            if ((branch_valid == 1'b1) &&
                                (branch_taken == 1'b1)) begin
                                pc <= instruction_pc_reg + immediate;
                            end
                            state <= STATE_FETCH;
                        end

                        OPCODE_JAL: begin
                            // JAL把返回地址写入rd，再跳到PC相对目标地址。
                            execute_result_reg   <= instruction_pc_reg + 32'd4;
                            writeback_enable_reg <= 1'b1;
                            pc                   <= instruction_pc_reg + immediate;
                            state                <= STATE_WRITEBACK;
                        end

                        OPCODE_JALR: begin
                            // JALR只允许funct3=000：rd保存返回地址PC+4，
                            // 跳转目标为rs1+符号扩展立即数，并按规范强制清零最低位。
                            if (funct3 == 3'b000) begin
                                execute_result_reg   <= instruction_pc_reg + 32'd4;
                                writeback_enable_reg <= 1'b1;
                                pc                   <= {alu_result[31:1], 1'b0};
                                state                <= STATE_WRITEBACK;
                            end else begin
                                state <= STATE_FETCH;
                            end
                        end

                        default: begin
                            // 暂未支持的指令先当作NOP跳过，后续逐类补齐。
                            state <= STATE_FETCH;
                        end
                    endcase
                end

                STATE_MEMORY: begin
                    if (memory_read_reg == 1'b1) begin
                        // 同步RAM在本拍末尾给出数据，再等一拍完成字节选择和扩展。
                        state <= STATE_MEMORY_WAIT;
                    end else begin
                        // Store写使能在本状态保持一个完整时钟，顶层在时钟沿写入。
                        memory_write_strobe_reg <= 4'b0000;
                        state                   <= STATE_FETCH;
                    end
                end

                STATE_MEMORY_WAIT: begin
                    // funct3决定读取宽度，以及短数据采用符号扩展还是零扩展。
                    case (funct3)
                        3'b000: execute_result_reg <= {{24{selected_load_byte[7]}},
                                                       selected_load_byte}; // LB
                        3'b001: execute_result_reg <= {{16{selected_load_half[15]}},
                                                       selected_load_half}; // LH
                        3'b010: execute_result_reg <= data_read_data;        // LW
                        3'b100: execute_result_reg <= {24'd0,
                                                       selected_load_byte}; // LBU
                        3'b101: execute_result_reg <= {16'd0,
                                                       selected_load_half}; // LHU
                        default: execute_result_reg <= 32'd0;
                    endcase

                    memory_read_reg      <= 1'b0;
                    writeback_enable_reg <= 1'b1;
                    state                <= STATE_WRITEBACK;
                end

                STATE_WRITEBACK: begin
                    // 寄存器堆会在本时钟沿写入，随后回到取指阶段。
                    writeback_enable_reg <= 1'b0;
                    state                 <= STATE_FETCH;
                end

                default: begin
                    state <= STATE_FETCH;
                end
            endcase
        end
    end

endmodule
