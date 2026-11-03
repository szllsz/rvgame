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
module rv32i_game_core #(
    parameter SERIAL_REGFILE = 0,
    parameter REGISTER_IMMEDIATE = 0,
    parameter FALLTHROUGH_FETCH = 0,
    parameter FAST_ADDRESS_REGIONS = 0,
    parameter REGISTER_CONTROL_FETCH = 0, // Fetch resolved control targets only after PC commit.
    parameter DECODE_MUL = 0, // Optional serialized multiply issue using decode operands.
    parameter FAST_LOW_MUL = 0, // Serialized MUL may return its registered low word one cycle early.
    parameter FAST_MUL_RETURN = 0, // Complete multiply at ready and prefetch its sequential successor.
    parameter ENABLE_ZMMUL = 0, // Optional hardware multiply; division remains software.
    parameter SYNC_INSTRUCTION = 0, // 1为地址寄存器+同步BRAM增加两拍取指等待；默认保留组合ROM
    parameter WAIT_STATES = 0, // 1在取指/Load阶段等待ready；Store仍仅用于本地RAM/MMIO
    parameter EARLY_FETCH = 0, // 1从FETCH发出请求，与原两拍取指准备重叠；ready最早在CAPTURE有效
    parameter EARLY_LOAD = 0, // 1仅对0x80xxxxxx DDR窗口从MEMORY发出Load请求，重叠一拍查找
    parameter FAST_EXEC = 0, // 1在EXECUTE直接写回整数运算/跳转结果，省去一拍WRITEBACK
    parameter FETCH_OVERLAP = 0, // 配合EARLY_FETCH/FAST_EXEC，直线ALU译码时请求下一条DDR指令
    parameter CONTROL_OVERLAP = 0, // 分支/跳转执行拍按实际目标启动DDR取指
    parameter MEMORY_OVERLAP = 0, // Store提交/Load写回拍启动下一条DDR取指，避开数据请求
    parameter LOCAL_LOAD_OVERLAP = 0, // 本地Load读取与DDR取指并行，并在ready拍直接写回
    parameter DECODE_CONTROL = 0, // 配合CONTROL_OVERLAP，在译码拍用当前寄存器值解析实际目标
    parameter EARLY_RESPONSE = 0, // 允许一拍缓存命中在EXECUTE/FETCH_WAIT返回，先缓冲或采样
    parameter FETCH_FOLD = 0, // 当前指令完成且下一条已返回时，直接进入DECODE
    parameter DECODE_RAM_LOAD = 0, // 仅0x20000000..0x2001ffff同步工作RAM在译码拍给地址
    parameter SYNC_RESET = 0, // 1为同步复位，reset_n须跨至少一个时钟沿；接口在复位期间立即禁止提交
    parameter DECODE_DDR_LOAD = 0, // DDR Load译码拍请求，EXECUTE/WAIT按ready直接写回
    parameter FAST_STORE = 0, // 本地SB/SH/SW在EXECUTE提交，RAM仍由SoC寄存后写入
    parameter STORE_DIRECT_ADDR = 0, // 快速Store仅需加法，旁路通用ALU结果选择
    parameter DDR_LOAD_HANDOFF = 0, // DDR Load成功返回沿同时交接下一条DDR取指，SoC/cache须支持next接口
    parameter DDR_HANDOFF_HINT = 0, // 等待期间提前给next地址，旧数据请求成功返回之前不交接
    parameter FAST_DECODE = 0, // DDR整数/控制指令在译码拍完成，后继ready时连续进入下一条译码
    parameter DECODE_STORE = 0 // 本地Store译码拍提交；SoC须对延后一拍的RAM写做字节转发
) (
    input  wire        clk,
    input  wire        reset_n,

    // 指令存储器接口：地址由PC给出，当前ROM为组合读取
    output wire [31:0] instruction_address,
    input  wire [31:0] instruction_data,
    output wire        instruction_request,
    input  wire        instruction_ready,
    output wire        instruction_next_hint,
    output wire [31:0] instruction_next_address,

    // 数据存储器/MMIO接口
    output wire [31:0] data_address,
    output wire [31:0] data_write_data,
    output wire [3:0]  data_write_strobe,
    output wire        data_write_enable,
    input  wire        data_write_ready,
    input  wire [31:0] data_read_data,
    output wire        data_read_request,
    output wire        data_address_is_ddr,
    input  wire        data_read_ready,

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
    reg [31:0] captured_immediate_reg;
    reg [31:0] instruction_pc_reg;
    reg [31:0] operand_a_reg;
    reg [31:0] operand_b_reg;
    reg [31:0] execute_result_reg;
    reg [31:0] memory_address_reg;
    reg [31:0] memory_write_data_reg;
    reg [3:0]  memory_write_strobe_reg;
    reg        memory_read_reg;
    reg        writeback_enable_reg;
    reg        overlap_pending;
    reg        overlap_buffer_valid;
    reg [31:0] overlap_instruction;

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
    wire [31:0] immediate_comb, incoming_immediate, overlap_immediate;
    assign immediate = REGISTER_IMMEDIATE ? captured_immediate_reg : immediate_comb;

    rv32i_imm_gen u_rv32i_imm_gen (
        .instruction(instruction_reg),
        .immediate  (immediate_comb)
    );
    // Decode before the capture edge, removing opcode selection from address ALUs.
    rv32i_imm_gen u_incoming_immediate (
        .instruction(instruction_data),.immediate(incoming_immediate)
    );
    rv32i_imm_gen u_overlap_immediate (
        .instruction(overlap_instruction),.immediate(overlap_immediate)
    );
`ifndef SYNTHESIS
    integer registered_immediate_checks=0;
    always @(posedge clk) begin
        if (REGISTER_IMMEDIATE && reset_n) begin
            if (captured_immediate_reg !== immediate_comb)
                $fatal(1,"Registered immediate is not aligned with the current instruction");
            if (registered_immediate_checks==0) $display("REGISTERED_IMMEDIATE_ORACLE_ACTIVE");
            registered_immediate_checks <= registered_immediate_checks+1;
        end
    end
`endif

    wire register_file_ready;
    wire [31:0] register_read_data1;
    wire [31:0] register_read_data2;
    wire        register_write_enable;
    wire        fast_execute_write;
    wire        fast_local_load_write;
    wire        fast_ddr_load_write;
    wire        fast_decode_write;
    wire        fast_decode_complete;
    wire [31:0] register_write_data;
    wire multiply_instruction = ENABLE_ZMMUL && opcode == OPCODE_OP &&
        funct7 == 7'b0000001 && !funct3[2];
    wire multiply_done;
    wire [31:0] multiply_result;
    reg multiply_started;
    wire fast_multiply_write = FAST_MUL_RETURN && FAST_EXEC && state == STATE_EXECUTE &&
        multiply_instruction && multiply_started && multiply_done;
    wire decode_multiply_issue = DECODE_MUL && FAST_DECODE && FAST_MUL_RETURN && FAST_EXEC &&
        WAIT_STATES && state == STATE_DECODE && instruction_pc_reg[31:24] == 8'h80 && multiply_instruction;
    wire multiply_start = reset_n && multiply_instruction &&
        (decode_multiply_issue || (state == STATE_EXECUTE && !multiply_started));
    always @(posedge clk) begin
        if (!reset_n || fast_multiply_write ||
            (state != STATE_EXECUTE && !decode_multiply_issue) || !multiply_instruction) multiply_started <= 0;
        else if (multiply_start) multiply_started <= 1;
    end
    generate if (ENABLE_ZMMUL) begin : g_multiply
        rv32mul_pipe #(.FAST_LOW(FAST_LOW_MUL)) u_multiply(.clk(clk),.reset_n(reset_n),.start(multiply_start),
            .op(funct3[1:0]),
            .lhs(decode_multiply_issue ? register_read_data1 : operand_a_reg),
            .rhs(decode_multiply_issue ? register_read_data2 : operand_b_reg),
            .done(multiply_done),.result(multiply_result));
    end else begin : g_no_multiply
        assign multiply_done = 1'b0;
        assign multiply_result = 32'd0;
    end endgenerate
    reg  [31:0] load_result;

    // 快速模式只在有效EXECUTE或原WRITEBACK时写一次；x0始终禁止写入。
    assign register_write_enable = register_file_ready && (!SYNC_RESET || reset_n) && ((state == STATE_WRITEBACK && writeback_enable_reg) ||
                                    fast_execute_write || fast_decode_write || fast_local_load_write || fast_ddr_load_write || fast_multiply_write) && (rd_address != 5'd0);

    generate if (SERIAL_REGFILE) begin: g_serial_regfile
        rv32i_serial_regfile #(.SYNC_RESET(SYNC_RESET)) u_regfile (
            .clk(clk),.reset_n(reset_n),.ready(register_file_ready),
            .read_addr1(rs1_address),.read_addr2(rs2_address),
            .read_data1(register_read_data1),.read_data2(register_read_data2),
            .write_enable(register_write_enable),.write_addr(rd_address),.write_data(register_write_data)
        );
    end else begin: g_valid_regfile
        assign register_file_ready = 1'b1;
    rv32i_game_regfile #(.SYNC_RESET(SYNC_RESET)) u_rv32i_regfile (
        .clk         (clk),
        .reset_n     (reset_n),
        .read_addr1  (rs1_address),
        .read_addr2  (rs2_address),
        .read_data1  (register_read_data1),
        .read_data2  (register_read_data2),
        .write_enable(register_write_enable),
        .write_addr  (rd_address),
        .write_data  (register_write_data)
    );
    end endgenerate

    reg  [31:0] alu_operand_a;
    reg  [31:0] alu_operand_b;
    reg  [3:0]  alu_operation;
    reg         r_type_valid;
    reg         i_type_valid;
    wire [31:0] alu_result;
    assign fast_execute_write = FAST_EXEC && state == STATE_EXECUTE &&
        (opcode == OPCODE_LUI || opcode == OPCODE_AUIPC ||
         (opcode == OPCODE_OP_IMM && i_type_valid) ||
         (opcode == OPCODE_OP && r_type_valid) || opcode == OPCODE_JAL ||
         (opcode == OPCODE_JALR && funct3 == 3'b000));
    assign fast_decode_write = FAST_DECODE && WAIT_STATES && FAST_EXEC && state == STATE_DECODE &&
        instruction_pc_reg[31:24] == 8'h80 &&
        (opcode == OPCODE_LUI || opcode == OPCODE_AUIPC ||
         (opcode == OPCODE_OP_IMM && i_type_valid) || (opcode == OPCODE_OP && r_type_valid) ||
         (DECODE_CONTROL && (opcode == OPCODE_JAL || (opcode == OPCODE_JALR && funct3 == 3'b000))));
    assign register_write_data = fast_multiply_write ? multiply_result : (fast_execute_write || fast_decode_write) ?
        ((opcode == OPCODE_JAL || opcode == OPCODE_JALR) ? instruction_pc_reg + 32'd4 : alu_result) :
        (fast_local_load_write || fast_ddr_load_write) ? load_result : execute_result_reg;

    // R型和I型指令都根据funct3选择ALU运算。
    // 移位指令还必须检查funct7，防止未定义编码误写寄存器。
    always @(*) begin
        alu_operand_a = FAST_DECODE && state == STATE_DECODE ? register_read_data1 : operand_a_reg;
        alu_operand_b = FAST_DECODE && state == STATE_DECODE ? register_read_data2 : operand_b_reg;
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
    assign fast_decode_complete = fast_decode_write || (FAST_DECODE && WAIT_STATES && FAST_EXEC &&
        DECODE_CONTROL && state == STATE_DECODE && instruction_pc_reg[31:24] == 8'h80 &&
        opcode == OPCODE_BRANCH && branch_valid);
    reg branch_taken;
    wire [31:0] control_operand_a = DECODE_CONTROL && state == STATE_DECODE ? register_read_data1 : operand_a_reg;
    wire [31:0] control_operand_b = DECODE_CONTROL && state == STATE_DECODE ? register_read_data2 : operand_b_reg;

    // 六条B型分支指令共用这一组比较逻辑。
    // BLT/BGE按有符号数比较，BLTU/BGEU按无符号数比较；两者不能混用。
    always @(*) begin
        branch_valid = 1'b1;
        branch_taken = 1'b0;

        case (funct3)
            3'b000: branch_taken = (control_operand_a == control_operand_b);                  // BEQ
            3'b001: branch_taken = (control_operand_a != control_operand_b);                  // BNE
            3'b100: branch_taken = ($signed(control_operand_a) < $signed(control_operand_b));  // BLT
            3'b101: branch_taken = ($signed(control_operand_a) >= $signed(control_operand_b)); // BGE
            3'b110: branch_taken = (control_operand_a < control_operand_b);                    // BLTU
            3'b111: branch_taken = (control_operand_a >= control_operand_b);                   // BGEU
            default: begin
                branch_valid = 1'b0;
                branch_taken = 1'b0;
            end
        endcase
    end

    // JALR只需要加法；直接计算目标，避免缓存请求经过通用ALU结果选择器。
    wire [31:0] jalr_sum = control_operand_a + immediate;
    wire [31:0] control_pc = opcode == OPCODE_JALR ? {jalr_sum[31:1], 1'b0} :
        (opcode == OPCODE_JAL || (opcode == OPCODE_BRANCH && branch_taken)) ?
        instruction_pc_reg + immediate : pc;
    wire registered_control_fetch = REGISTER_CONTROL_FETCH && WAIT_STATES && EARLY_FETCH && FAST_EXEC;
    wire start_control = !registered_control_fetch && CONTROL_OVERLAP && WAIT_STATES && EARLY_FETCH && FAST_EXEC &&
        state == STATE_EXECUTE && control_pc[31:24] == 8'h80 &&
        ((opcode == OPCODE_BRANCH && branch_valid) || opcode == OPCODE_JAL ||
         (opcode == OPCODE_JALR && funct3 == 3'b000));
    // Only a resolved not-taken branch uses the already-registered sequential PC.
    // Taken branches and jumps retain the registered target boundary.
    wire start_fallthrough = FALLTHROUGH_FETCH && registered_control_fetch && DECODE_CONTROL &&
        CONTROL_OVERLAP && state == STATE_DECODE && pc[31:24] == 8'h80 &&
        opcode == OPCODE_BRANCH && branch_valid && !branch_taken;
    wire start_decode_control = start_fallthrough || (!registered_control_fetch && DECODE_CONTROL && CONTROL_OVERLAP && WAIT_STATES && EARLY_FETCH && FAST_EXEC &&
        state == STATE_DECODE && control_pc[31:24] == 8'h80 &&
        ((opcode == OPCODE_BRANCH && branch_valid) || opcode == OPCODE_JAL ||
         (opcode == OPCODE_JALR && funct3 == 3'b000)));
    // Address selection must not wait for the full target's DDR-range check.
    // Requests and architectural commits retain the original qualified enables.
    wire control_address_select = !registered_control_fetch && CONTROL_OVERLAP && WAIT_STATES && EARLY_FETCH && FAST_EXEC &&
        (state == STATE_EXECUTE || (DECODE_CONTROL && state == STATE_DECODE)) &&
        ((opcode == OPCODE_BRANCH && branch_valid) || opcode == OPCODE_JAL ||
         (opcode == OPCODE_JALR && funct3 == 3'b000));
    assign instruction_address = control_address_select ? control_pc : pc;
    wire start_overlap = FETCH_OVERLAP && WAIT_STATES && EARLY_FETCH && FAST_EXEC &&
        state == STATE_DECODE && pc[31:24] == 8'h80 &&
        (opcode == OPCODE_LUI || opcode == OPCODE_AUIPC ||
         (opcode == OPCODE_OP_IMM && i_type_valid) || (opcode == OPCODE_OP && r_type_valid) ||
         (FAST_MUL_RETURN && multiply_instruction));
    wire [31:0] decode_load_address = register_read_data1 + immediate;
    // Load/store immediates are signed 12-bit values. Bits above bit 11 change
    // only by carry12 - sign; recognize crossings at each region's boundary.
    wire [12:0] decode_low_sum = {1'b0,register_read_data1[11:0]} + {1'b0,immediate[11:0]};
    wire ddr_carry = !immediate[11] && decode_low_sum[12] && (&register_read_data1[23:12]);
    wire ddr_borrow = immediate[11] && !decode_low_sum[12] && !(|register_read_data1[23:12]);
    wire ram_carry = !immediate[11] && decode_low_sum[12] && (&register_read_data1[16:12]);
    wire ram_borrow = immediate[11] && !decode_low_sum[12] && !(|register_read_data1[16:12]);
    wire decode_ddr_region = FAST_ADDRESS_REGIONS ?
        ((register_read_data1[31:24]==8'h80 && !ddr_carry && !ddr_borrow) ||
         (register_read_data1[31:24]==8'h7f && ddr_carry) ||
         (register_read_data1[31:24]==8'h81 && ddr_borrow)) : decode_load_address[31:24]==8'h80;
    wire decode_ram_region = FAST_ADDRESS_REGIONS ?
        ((register_read_data1[31:17]==15'h1000 && !ram_carry && !ram_borrow) ||
         (register_read_data1[31:17]==15'h0fff && ram_carry) ||
         (register_read_data1[31:17]==15'h1001 && ram_borrow)) : decode_load_address[31:17]==15'h1000;
    wire decode_store_candidate = DECODE_STORE && FAST_DECODE && FAST_STORE && WAIT_STATES &&
        state == STATE_DECODE && instruction_pc_reg[31:24] == 8'h80 && opcode == OPCODE_STORE &&
        !decode_ddr_region && (funct3 == 3'b000 || funct3 == 3'b001 || funct3 == 3'b010);
    wire decode_store_commit = decode_store_candidate && data_write_ready;
    // Select the address without waiting for that address's range check.
    // Request, write strobes, and retirement still use the qualified enables.
    wire decode_store_address_select = DECODE_STORE && FAST_DECODE && FAST_STORE && WAIT_STATES &&
        state == STATE_DECODE && instruction_pc_reg[31:24] == 8'h80 && opcode == OPCODE_STORE &&
        (funct3 == 3'b000 || funct3 == 3'b001 || funct3 == 3'b010);
    wire [31:0] store_address = decode_store_address_select ? decode_load_address :
        STORE_DIRECT_ADDR ? operand_a_reg + immediate : alu_result;
    wire [31:0] store_operand_b = decode_store_candidate ? register_read_data2 : operand_b_reg;
    wire execute_store_commit = FAST_STORE && state == STATE_EXECUTE && opcode == OPCODE_STORE &&
        store_address[31:24] != 8'h80 && (funct3 == 3'b000 || funct3 == 3'b001 || funct3 == 3'b010);
    wire fast_store_commit = decode_store_commit || execute_store_commit;
    wire start_memory = MEMORY_OVERLAP && WAIT_STATES && EARLY_FETCH && pc[31:24] == 8'h80 &&
        ((state == STATE_MEMORY && !memory_read_reg && |memory_write_strobe_reg) ||
         (state == STATE_WRITEBACK && opcode == OPCODE_LOAD && writeback_enable_reg) || fast_store_commit);
    wire start_decode_load = DECODE_RAM_LOAD && LOCAL_LOAD_OVERLAP && WAIT_STATES &&
        EARLY_FETCH && EARLY_RESPONSE && state == STATE_DECODE && pc[31:24] == 8'h80 &&
        decode_ram_region && opcode == OPCODE_LOAD &&
        (funct3 == 3'b000 || funct3 == 3'b001 || funct3 == 3'b010 || funct3 == 3'b100 || funct3 == 3'b101);
    wire start_decode_ddr_load = DECODE_DDR_LOAD && WAIT_STATES && EARLY_LOAD && EARLY_RESPONSE &&
        state == STATE_DECODE && decode_ddr_region && opcode == OPCODE_LOAD &&
        (funct3 == 3'b000 || funct3 == 3'b001 || funct3 == 3'b010 || funct3 == 3'b100 || funct3 == 3'b101);
    wire local_load_overlap = LOCAL_LOAD_OVERLAP && WAIT_STATES && EARLY_FETCH &&
        pc[31:24] == 8'h80 && memory_address_reg[31:24] != 8'h80 && memory_read_reg;
    wire start_local_load = local_load_overlap && state == STATE_MEMORY;
    wire decoded_ram_load = DECODE_RAM_LOAD && EARLY_RESPONSE && local_load_overlap &&
        memory_address_reg[31:17] == 15'h1000;
    wire decoded_ddr_load = DECODE_DDR_LOAD && WAIT_STATES && EARLY_LOAD && EARLY_RESPONSE &&
        memory_read_reg && memory_address_reg[31:24] == 8'h80;
    assign fast_ddr_load_write = decoded_ddr_load && data_read_ready &&
        (state == STATE_EXECUTE || state == STATE_MEMORY_WAIT);
    wire start_ddr_handoff = DDR_LOAD_HANDOFF && EARLY_FETCH && pc[31:24] == 8'h80 && fast_ddr_load_write;
    wire ddr_handoff_hint = DDR_HANDOFF_HINT && DDR_LOAD_HANDOFF && EARLY_FETCH &&
        pc[31:24] == 8'h80 && decoded_ddr_load && (state == STATE_EXECUTE || state == STATE_MEMORY_WAIT);
    // next仅提示缓存内部读地址；不经过通用取指请求/分支目标选择，交接仍由旧请求ready控制。
    assign instruction_next_hint = ddr_handoff_hint && (!SYNC_RESET || reset_n) && !overlap_buffer_valid;
    assign instruction_next_address = pc;
    assign fast_local_load_write = local_load_overlap && data_read_ready &&
        (state == STATE_MEMORY_WAIT || (decoded_ram_load && state == STATE_EXECUTE));
    wire fold_next_instruction = FETCH_FOLD && WAIT_STATES && ((overlap_pending &&
        (overlap_buffer_valid || instruction_ready) &&
        ((state == STATE_EXECUTE && (fast_execute_write || fast_multiply_write || (opcode == OPCODE_BRANCH && branch_valid))) ||
         fast_local_load_write)) ||
        (fast_decode_complete && (start_overlap || start_decode_control) && instruction_ready) ||
        (fast_ddr_load_write && start_ddr_handoff && instruction_ready) ||
        (decode_store_commit && start_memory && instruction_ready));
    // 不猜测分支；请求地址保持到FETCH/CAPTURE握手完成。
    // EARLY_RESPONSE支持同步BRAM的一拍命中；默认仍按原两拍返回。
    assign instruction_request = WAIT_STATES && register_file_ready && (!SYNC_RESET || reset_n) && !overlap_buffer_valid &&
        (start_overlap || start_control || start_decode_control || start_decode_load || start_memory || start_local_load || start_ddr_handoff || overlap_pending || state == STATE_FETCH_CAPTURE ||
         (EARLY_FETCH && (state == STATE_FETCH || state == STATE_FETCH_WAIT)));
    // EXECUTE末尾地址已寄存；DDR缓存ready最早在MEMORY_WAIT有效。
    // 工作RAM可提前给地址，实际请求从EXECUTE开始；其他本地窗口保留原时序。
    assign data_read_request = WAIT_STATES && register_file_ready && (!SYNC_RESET || reset_n) &&
        (start_decode_ddr_load || (memory_read_reg &&
         (state == STATE_MEMORY_WAIT || ((decoded_ram_load || decoded_ddr_load) && state == STATE_EXECUTE) ||
          (EARLY_LOAD && state == STATE_MEMORY && memory_address_reg[31:24] == 8'h80))));
    wire decode_load_address_select = state == STATE_DECODE && opcode == OPCODE_LOAD &&
        (funct3 == 3'b000 || funct3 == 3'b001 || funct3 == 3'b010 || funct3 == 3'b100 || funct3 == 3'b101) &&
        ((DECODE_RAM_LOAD && LOCAL_LOAD_OVERLAP && WAIT_STATES && EARLY_FETCH && EARLY_RESPONSE &&
          pc[31:24] == 8'h80) || (DECODE_DDR_LOAD && WAIT_STATES && EARLY_LOAD && EARLY_RESPONSE));
    wire execute_store_address_select = FAST_STORE && state == STATE_EXECUTE && opcode == OPCODE_STORE &&
        (funct3 == 3'b000 || funct3 == 3'b001 || funct3 == 3'b010);
    assign data_address = (decode_store_address_select || execute_store_address_select) ? store_address :
        decode_load_address_select ? decode_load_address : memory_address_reg;
    assign data_address_is_ddr = FAST_ADDRESS_REGIONS &&
        (decode_load_address_select || decode_store_address_select) ? decode_ddr_region : data_address[31:24]==8'h80;
    assign data_write_data      = fast_store_commit ?
        (funct3 == 3'b000 ? {4{store_operand_b[7:0]}} :
         funct3 == 3'b001 ? {2{store_operand_b[15:0]}} : store_operand_b) : memory_write_data_reg;
    assign data_write_strobe    = (!SYNC_RESET || reset_n) ?
        (fast_store_commit ? (funct3 == 3'b000 ? 4'b0001 << store_address[1:0] :
                             funct3 == 3'b001 ? (store_address[1] ? 4'b1100 : 4'b0011) : 4'b1111) :
         state == STATE_MEMORY ? memory_write_strobe_reg : 4'b0000) : 4'b0000;
    assign data_write_enable    = |data_write_strobe;

    // debug_pc使用“当前指令被取出时的PC”，这样ILA里的PC和instruction一一对应。
    // 如果直接观察pc寄存器，在译码/执行阶段看到的会是已经加4后的下一条地址。
    assign debug_pc             = instruction_pc_reg;
    assign debug_instruction    = instruction_reg;
    assign debug_state          = state;
    assign debug_reg_write      = register_write_enable;
    assign debug_reg_write_addr = rd_address;
    assign debug_reg_write_data = register_write_data;

    // RAM按32位字返回数据，Load指令再根据地址低两位选择字节或半字。
    wire [7:0] selected_load_byte;
    wire [15:0] selected_load_half;

    assign selected_load_byte = (memory_address_reg[1:0] == 2'b00) ? data_read_data[7:0]   :
                                (memory_address_reg[1:0] == 2'b01) ? data_read_data[15:8]  :
                                (memory_address_reg[1:0] == 2'b10) ? data_read_data[23:16] :
                                                                    data_read_data[31:24];
    assign selected_load_half = (memory_address_reg[1] == 1'b0) ?
                                data_read_data[15:0] : data_read_data[31:16];
    always @(*) begin
        case (funct3)
            3'b000: load_result = {{24{selected_load_byte[7]}}, selected_load_byte}; // LB
            3'b001: load_result = {{16{selected_load_half[15]}}, selected_load_half}; // LH
            3'b010: load_result = data_read_data; // LW
            3'b100: load_result = {24'd0, selected_load_byte}; // LBU
            3'b101: load_result = {16'd0, selected_load_half}; // LHU
            default: load_result = 32'd0;
        endcase
    end

    // 访存地址寄存器保持同步复位；工作RAM预取时可在DECODE旁路地址。
    // 预取地址先锁存，其他访存仍在EXECUTE时钟沿更新。
    always @(posedge clk) begin
        if (reset_n == 1'b0) memory_address_reg <= 32'd0;
        else if (start_decode_load || start_decode_ddr_load) memory_address_reg <= decode_load_address;
        else if (state == STATE_EXECUTE &&
                 ((opcode == OPCODE_LOAD &&
                   (funct3 == 3'b000 || funct3 == 3'b001 || funct3 == 3'b010 ||
                    funct3 == 3'b100 || funct3 == 3'b101)) ||
                  (opcode == OPCODE_STORE &&
                   (funct3 == 3'b000 || funct3 == 3'b001 || funct3 == 3'b010))))
            memory_address_reg <= alu_result;
    end

    // 两种复位模式共用时钟沿逻辑，避免维护两份CPU状态机。
    task step_cpu;
    begin
        if (reset_n == 1'b0) begin
            state                 <= STATE_FETCH;
            pc                    <= 32'd0;
            instruction_reg       <= 32'h0000_0013;  // ADDI x0,x0,0，即NOP
            captured_immediate_reg <= 32'd0;
            instruction_pc_reg    <= 32'd0;
            operand_a_reg         <= 32'd0;
            operand_b_reg         <= 32'd0;
            execute_result_reg    <= 32'd0;
            memory_write_data_reg <= 32'd0;
            memory_write_strobe_reg <= 4'b0000;
            memory_read_reg       <= 1'b0;
            writeback_enable_reg  <= 1'b0;
            overlap_pending       <= 1'b0;
            overlap_buffer_valid  <= 1'b0;
            overlap_instruction   <= 32'd0;
        end else if (register_file_ready) begin
            if (start_ddr_handoff) overlap_pending <= 1'b1;
            // 提前返回的指令只采样一次；本地ready延迟时保持缓冲，直到当前指令提交。
            if (((LOCAL_LOAD_OVERLAP && state == STATE_MEMORY_WAIT) ||
                 (EARLY_RESPONSE && state == STATE_EXECUTE)) && overlap_pending &&
                !overlap_buffer_valid && instruction_ready) begin
                overlap_instruction <= instruction_data;
                overlap_buffer_valid <= 1'b1;
            end
            case (state)
                STATE_FETCH: begin
                    // The registered PC now owns the control fetch; ready refers to that PC.
                    // Reuse the normal pending-fetch capture path, without issuing a speculative target.
                    if (overlap_pending || (registered_control_fetch && pc[31:24] == 8'h80)) begin
                        memory_write_strobe_reg <= 4'b0000;
                        memory_read_reg <= 1'b0;
                        writeback_enable_reg <= 1'b0;
                        if (overlap_buffer_valid || instruction_ready) begin
                            instruction_reg <= overlap_buffer_valid ? overlap_instruction : instruction_data;
                            captured_immediate_reg <= overlap_buffer_valid ? overlap_immediate : incoming_immediate;
                            instruction_pc_reg <= pc;
                            pc <= pc + 32'd4;
                            overlap_pending <= 1'b0;
                            overlap_buffer_valid <= 1'b0;
                            state <= STATE_DECODE;
                        end else state <= STATE_FETCH_CAPTURE;
                    end else if (SYNC_INSTRUCTION || WAIT_STATES) begin
                        memory_write_strobe_reg <= 4'b0000;
                        memory_read_reg <= 1'b0;
                        writeback_enable_reg <= 1'b0;
                        state <= STATE_FETCH_WAIT;
                    end else begin
                    // 保存当前指令及其PC，同时让PC默认指向下一条指令。
                    instruction_reg      <= instruction_data;
                    captured_immediate_reg <= incoming_immediate;
                    instruction_pc_reg   <= pc;
                    pc                   <= pc + 32'd4;
                    memory_write_strobe_reg <= 4'b0000;
                    memory_read_reg      <= 1'b0;
                    writeback_enable_reg <= 1'b0;
                    state                <= STATE_DECODE;
                    end
                end

                STATE_FETCH_WAIT: begin
                    if (EARLY_RESPONSE && WAIT_STATES && pc[31:24] == 8'h80 && instruction_ready) begin
                        instruction_reg <= instruction_data;
                        captured_immediate_reg <= incoming_immediate;
                        instruction_pc_reg <= pc;
                        pc <= pc + 32'd4;
                        overlap_pending <= 1'b0;
                        overlap_buffer_valid <= 1'b0;
                        state <= STATE_DECODE;
                    end else state <= STATE_FETCH_CAPTURE;
                end

                STATE_FETCH_CAPTURE: begin
                    if (!WAIT_STATES || overlap_buffer_valid || instruction_ready) begin
                    instruction_reg <= overlap_buffer_valid ? overlap_instruction : instruction_data;
                    captured_immediate_reg <= overlap_buffer_valid ? overlap_immediate : incoming_immediate;
                    instruction_pc_reg <= pc;
                    pc <= pc + 32'd4;
                    overlap_pending <= 1'b0;
                    overlap_buffer_valid <= 1'b0;
                    state <= STATE_DECODE;
                    end
                end

                STATE_DECODE: begin
                    // 寄存器堆是组合读，本拍把两个源操作数锁存下来。
                    operand_a_reg <= register_read_data1;
                    operand_b_reg <= register_read_data2;
                    if (start_overlap || start_decode_control || start_decode_load ||
                        (decode_store_commit && start_memory)) overlap_pending <= 1'b1;
                    if (start_decode_load || start_decode_ddr_load) memory_read_reg <= 1'b1;
                    state         <= STATE_EXECUTE;
                    if (decode_store_candidate && !data_write_ready) state <= STATE_DECODE;
                    if (fast_decode_complete || decode_store_commit) begin
                        // 写回沿与后继译码边界重合；下一拍组合读能看到刚提交的寄存器，无需转发。
                        if (opcode == OPCODE_BRANCH || opcode == OPCODE_JAL || opcode == OPCODE_JALR)
                            pc <= control_pc;
                        memory_read_reg <= 1'b0;
                        memory_write_strobe_reg <= 4'b0000;
                        writeback_enable_reg <= 1'b0;
                        state <= STATE_FETCH;
                    end
                end

                STATE_EXECUTE: begin
                    if (start_control || start_memory) overlap_pending <= 1'b1;
                    case (opcode)
                        OPCODE_LUI: begin
                            execute_result_reg   <= alu_result;
                            writeback_enable_reg <= 1'b1;
                            state                <= FAST_EXEC ? STATE_FETCH : STATE_WRITEBACK;
                        end

                        OPCODE_AUIPC: begin
                            // AUIPC：rd=当前指令PC+(20位立即数左移12位)。
                            execute_result_reg   <= alu_result;
                            writeback_enable_reg <= 1'b1;
                            state                <= FAST_EXEC ? STATE_FETCH : STATE_WRITEBACK;
                        end

                        OPCODE_OP_IMM: begin
                            // 9条I型整数运算共用ALU和寄存器写回路径。
                            if (i_type_valid == 1'b1) begin
                                execute_result_reg   <= alu_result;
                                writeback_enable_reg <= 1'b1;
                                state                <= FAST_EXEC ? STATE_FETCH : STATE_WRITEBACK;
                            end else begin
                                state <= STATE_FETCH;
                            end
                        end

                        OPCODE_OP: begin
                            // Optional ready-edge completion retains the registered arithmetic result.
                            if (multiply_instruction) begin
                                if (multiply_started && multiply_done) begin
                                    execute_result_reg <= multiply_result;
                                    writeback_enable_reg <= !fast_multiply_write;
                                    state <= fast_multiply_write ? STATE_FETCH : STATE_WRITEBACK;
                                end
                            end else if (r_type_valid == 1'b1) begin
                                execute_result_reg   <= alu_result;
                                writeback_enable_reg <= 1'b1;
                                state                <= FAST_EXEC ? STATE_FETCH : STATE_WRITEBACK;
                            end else begin
                                state <= STATE_FETCH;
                            end
                        end

                        OPCODE_STORE: begin
                            // 字节写使能决定32位总线中哪些字节真正写入RAM。
                            // 题目不要求非对齐访问，因此软件应保证SH按2字节、SW按4字节对齐。
                            if (fast_store_commit) begin
                                memory_write_strobe_reg <= 4'b0000;
                                state <= STATE_FETCH;
                            end else case (funct3)
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
                                    if (decoded_ram_load || decoded_ddr_load) begin
                                        if (data_read_ready) begin
                                            execute_result_reg <= load_result;
                                            memory_read_reg <= 1'b0;
                                            writeback_enable_reg <= 1'b0;
                                            state <= STATE_FETCH;
                                        end else state <= STATE_MEMORY_WAIT;
                                    end else begin
                                        memory_read_reg <= 1'b1;
                                        state <= STATE_MEMORY;
                                    end
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
                            state                <= FAST_EXEC ? STATE_FETCH : STATE_WRITEBACK;
                        end

                        OPCODE_JALR: begin
                            // JALR只允许funct3=000：rd保存返回地址PC+4，
                            // 跳转目标为rs1+符号扩展立即数，并按规范强制清零最低位。
                            if (funct3 == 3'b000) begin
                                execute_result_reg   <= instruction_pc_reg + 32'd4;
                                writeback_enable_reg <= 1'b1;
                                pc                   <= {jalr_sum[31:1], 1'b0};
                                state                <= FAST_EXEC ? STATE_FETCH : STATE_WRITEBACK;
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
                    if (start_memory || start_local_load) overlap_pending <= 1'b1;
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
                    if (!WAIT_STATES || data_read_ready) begin
                    execute_result_reg <= load_result;

                    memory_read_reg      <= 1'b0;
                    writeback_enable_reg <= !(fast_local_load_write || fast_ddr_load_write);
                    state                <= (fast_local_load_write || fast_ddr_load_write) ? STATE_FETCH : STATE_WRITEBACK;
                    end
                end

                STATE_WRITEBACK: begin
                    if (start_memory) overlap_pending <= 1'b1;
                    // 寄存器堆会在本时钟沿写入，随后回到取指阶段。
                    writeback_enable_reg <= 1'b0;
                    state                 <= STATE_FETCH;
                end

                default: begin
                    state <= STATE_FETCH;
                end
            endcase
            // 运算/Load仍在原提交沿写回；下一拍读取新的寄存器值，无需猜测或转发。
            if (fold_next_instruction) begin
                instruction_reg <= overlap_buffer_valid ? overlap_instruction : instruction_data;
                captured_immediate_reg <= overlap_buffer_valid ? overlap_immediate : incoming_immediate;
                instruction_pc_reg <= instruction_address;
                pc <= instruction_address + 32'd4;
                memory_write_strobe_reg <= 4'b0000;
                memory_read_reg <= 1'b0;
                writeback_enable_reg <= 1'b0;
                overlap_pending <= 1'b0;
                overlap_buffer_valid <= 1'b0;
                state <= STATE_DECODE;
            end
        end
    end
    endtask

    generate if (SYNC_RESET) begin: g_sync_reset
        always @(posedge clk) step_cpu();
    end else begin: g_async_reset
        always @(posedge clk or negedge reset_n) step_cpu();
    end endgenerate

endmodule

// 本工程私有寄存器堆，保留教学版组合读/x0行为；共享教学源码不改动。
// Experimental FPGA mapping. Valid bits preserve the original reset-to-zero API.
// RAM inference approach also used by lowRISC ibex_register_file_fpga (Apache-2.0).
// Implementation below is local; it retains runtime reset unlike that reference.
module rv32i_game_regfile #(parameter SYNC_RESET=0) (
    input wire clk,
    input wire reset_n,
    input wire [4:0] read_addr1,
    input wire [4:0] read_addr2,
    output wire [31:0] read_data1,
    output wire [31:0] read_data2,
    input wire write_enable,
    input wire [4:0] write_addr,
    input wire [31:0] write_data
);
    (* ram_style = "distributed" *) reg [31:0] words [0:31];
    reg [31:0] valid;
    wire commit = reset_n && write_enable && write_addr != 0;
    assign read_data1 = read_addr1 != 0 && valid[read_addr1] ? words[read_addr1] : 32'd0;
    assign read_data2 = read_addr2 != 0 && valid[read_addr2] ? words[read_addr2] : 32'd0;
    always @(posedge clk) begin
        if (commit) words[write_addr] <= write_data;
    end
    generate if (SYNC_RESET) begin: g_sync_reset
        always @(posedge clk) begin
            if (!reset_n) valid <= 0;
            else if (commit) valid[write_addr] <= 1'b1;
        end
    end else begin: g_async_reset
        always @(posedge clk or negedge reset_n) begin
            if (!reset_n) valid <= 0;
            else if (commit) valid[write_addr] <= 1'b1;
        end
    end endgenerate
endmodule

`timescale 1ns/1ps
// Experimental Zmmul arithmetic unit. Two stages, plus optional input register.
// op: MUL=0, MULH=1, MULHSU=2, MULHU=3. Reset cancels all pending results.
module rv32mul_pipe #(parameter FAST_LOW=0,parameter REGISTER_INPUT=0) (
    input wire clk,
    input wire reset_n,
    input wire start,
    input wire [1:0] op,
    input wire [31:0] lhs,
    input wire [31:0] rhs,
    output wire done,
    output wire [31:0] result
);
    // Input payload remains free-running; validity alone carries accepted ownership.
    // REGISTER_INPUT adds one cycle to both result classes, without changing ports.
    reg [31:0] input_lhs,input_rhs;
    reg [1:0] input_op;
    reg input_pending;
    wire [31:0] arithmetic_lhs=REGISTER_INPUT ? input_lhs : lhs;
    wire [31:0] arithmetic_rhs=REGISTER_INPUT ? input_rhs : rhs;
    wire [1:0] arithmetic_op=REGISTER_INPUT ? input_op : op;
    wire arithmetic_start=REGISTER_INPUT ? input_pending : start;
    always @(posedge clk) begin
        input_lhs<=lhs;input_rhs<=rhs;input_op<=op;
        if(!reset_n) input_pending<=0;
        else input_pending<=start;
    end
    // FAST_LOW is only for a serialized owner: issue the next request after completion.
    reg result_ready;
    reg [31:0] result_reg;
    reg pending;
    reg [1:0] saved_op;
    reg [31:0] correction_a,correction_b;
    (* use_dsp = "yes" *) reg [63:0] product;
    assign done = FAST_LOW && saved_op==0 ? pending : result_ready;
    assign result = FAST_LOW && saved_op==0 ? product[31:0] : result_reg;
    // Arithmetic is speculative; pending/result_ready retain exact start ownership.
    // Only done-valid results are consumed. Reset clears validity, not DSP payload.
    always @(posedge clk) product<=$unsigned(arithmetic_lhs)*$unsigned(arithmetic_rhs);
    // Correction payload is consumed only with the preceding valid product.
    // Keep saved_op start-owned: FAST_LOW done selection must survive idle operands.
    always @(posedge clk) begin
        correction_a<=((arithmetic_op==1 || arithmetic_op==2) && arithmetic_lhs[31]) ? arithmetic_rhs : 32'd0;
        correction_b<=(arithmetic_op==1 && arithmetic_rhs[31]) ? arithmetic_lhs : 32'd0;
    end
    always @(posedge clk) begin
        if (!reset_n) begin
            pending<=0;
            result_ready<=0;
        end else begin
            pending<=arithmetic_start;
            result_ready<=pending;
            if (arithmetic_start) begin
                saved_op<=arithmetic_op;
            end
            if (pending)
                result_reg<=saved_op==0 ? product[31:0] : product[63:32]-correction_a-correction_b;
        end
    end
endmodule

// Clear all physical words before releasing the CPU after every reset/host hold.
// This removes the per-address reset-valid mux from normal register reads.
module rv32i_serial_regfile #(parameter SYNC_RESET=0) (
    input wire clk, input wire reset_n, output wire ready,
    input wire [4:0] read_addr1, input wire [4:0] read_addr2,
    output wire [31:0] read_data1, output wire [31:0] read_data2,
    input wire write_enable, input wire [4:0] write_addr, input wire [31:0] write_data
);
    (* ram_style = "distributed" *) reg [31:0] words [0:31];
    reg [4:0] clear_index;
    reg initialized;
    assign ready = reset_n && initialized;
    assign read_data1 = ready && read_addr1!=0 ? words[read_addr1] : 32'd0;
    assign read_data2 = ready && read_addr2!=0 ? words[read_addr2] : 32'd0;
    always @(posedge clk) begin
        if (reset_n) begin
            if (!initialized) words[clear_index] <= 32'd0;
            else if (write_enable && write_addr!=0) words[write_addr] <= write_data;
        end
    end
    task step_initialize;
    begin
        if (!reset_n) begin clear_index<=0;initialized<=0;end
        else if (!initialized) begin
            if (clear_index==31) initialized<=1;
            else clear_index<=clear_index+1'b1;
        end
    end
    endtask
    generate if (SYNC_RESET) begin: g_sync_initialize
        always @(posedge clk) step_initialize();
    end else begin: g_async_initialize
        always @(posedge clk or negedge reset_n) step_initialize();
    end endgenerate
endmodule
