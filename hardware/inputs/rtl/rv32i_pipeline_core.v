`timescale 1ns/1ps
// Experimental five main stages: registered IF -> decode queue -> EX -> MEM -> WB.
// Decode/execute/writeback may hold different instructions simultaneously.
// A two-entry decode queue reserves room for each outstanding fetch response.
// Shared I/D service is serialized explicitly; redirected fetches are drained.
// RV32I/Zmmul candidate plus serialized machine CSR/software traps and MEM-aware IRQ drain.
module rv32i_pipeline_core #(
    parameter SYNC_RESET=1,
    parameter LOCAL_FETCH_WAIT=2,
    parameter CHECK_FETCH_PERMISSION=0,
    parameter STATIC_CONTROL_PREDICTION=0,
    parameter OVERLAP_LOCAL_LOAD=0,
    parameter PREDECODED_PREDICTION=0,
    parameter PREFETCH_LOCAL_RAM=0,
    parameter INDIRECT_TARGET_CACHE=0,
    parameter DYNAMIC_BRANCH_PREDICTION=0,
    parameter SEPARATE_LOCAL_HINT_PORT=0,
    parameter EX_RESULT_BYPASS=1,
    parameter DRAIN_PERIPHERAL_WRITES=0,
    parameter SEPARATE_FETCH_PERMISSION=0,
    parameter ID_INDEPENDENT_LOW_MUL=0,
    parameter MEM_BRANCH_RESOLVE=0,
    parameter CORRELATED_DIRECTION=1,
    parameter REDIRECT_RESPONSE_HANDOFF=0,
    parameter EMPTY_FETCH_BYPASS=0,
    parameter ALIGNED_DUAL_ALU=0,
    parameter WIDE_FIRST_SLOT=1,
    parameter EARLY_MUL_LOCAL_LOAD=1
)(
    input wire clk,input wire reset_n,
    input wire irq_external,input wire irq_software,input wire irq_timer,
    input wire [63:0] platform_time,
    input wire peripheral_quiescent,
    output wire [31:0] data_permission_address,output wire data_permission_write,
    input wire data_access_permitted,
    input wire instruction_access_error,data_access_error,
    output wire [31:0] instruction_address,input wire [31:0] instruction_data,
    output wire instruction_request,input wire instruction_ready,
    input wire [31:0] instruction_prediction,
    input wire instruction_pair_valid,input wire [31:0] instruction_pair_data,
    output wire [31:0] instruction_permission_address,
    input wire instruction_permitted,instruction_next_permitted,output wire instruction_fault,
    output wire instruction_next_hint,output wire [31:0] instruction_next_address,
    output wire [31:0] data_address,output wire [31:0] data_write_data,
    output wire [3:0] data_write_strobe,output wire data_write_enable,
    output wire data_write_intent,output wire [3:0] data_write_mask,
    input wire data_write_ready,input wire [31:0] data_read_data,
    input wire [31:0] local_load_bypass_data,
    output wire data_read_request,output wire data_address_is_ddr,input wire data_read_ready,
    output wire local_read_hint,output wire [31:0] local_read_address,input wire local_read_hint_ready,
    output reg local_load_hint_first,
    output wire [31:0] debug_pc,output wire [31:0] debug_instruction,
    output wire [2:0] debug_state,output wire debug_reg_write,
    output wire [4:0] debug_reg_write_addr,output wire [31:0] debug_reg_write_data,
    output wire retire_valid,output wire [31:0] retire_pc,output wire [31:0] retire_instruction,
    output wire retire_second_valid,output wire [31:0] retire_second_pc,retire_second_instruction,
    output wire retire_second_reg_write,output wire [4:0] retire_second_rd,
    output wire [31:0] retire_second_data
);
    localparam PORT_IDLE=0,PORT_FETCH=1,PORT_LOAD=2;
    reg [1:0] port_owner;
    reg [31:0] fetch_pc,fetch_owner_pc;
    reg [3:0] fetch_age;
    reg fetch_killed,load_started;
    reg fetch_admitted;
    reg if_valid;
    // A pair is one owned entry: ordinary ALU/LSU/Zmmul first, independent ALU second.
    // Both results wait for the older slot; a first-slot fault discards the pair.
    reg if_pair,id_pair,skid_pair,ex_pair,mem_pair,wb_pair;
    reg [31:0] if_second_instruction,id_second_instruction,skid_second_instruction;
    reg [31:0] ex_second_instruction,mem_second_instruction,wb_second_instruction;
    reg [31:0] ex_second_a,ex_second_b,ex_second_immediate,mem_second_result,wb_second_data;
    reg [3:0] ex_second_operation;
    wire [31:0] rf_c,rf_d,id_second_immediate,ex_second_result;
    wire rf_pair_ready;
    wire [4:0] id_second_rs1=id_second_instruction[19:15],id_second_rs2=id_second_instruction[24:20];
    wire [4:0] ex_second_rd=ex_second_instruction[11:7];
    wire [4:0] mem_second_rd=mem_second_instruction[11:7];
    wire [4:0] wb_second_rd=wb_second_instruction[11:7];
    reg if_access_error,id_access_error,skid_access_error,ex_access_error;
    reg [31:0] if_pc,if_instruction,if_prediction;
    reg if_history_hit,if_history_taken,if_indirect_hit;
    reg [31:0] if_history_target;
    reg [30:0] if_indirect_target;
    // One outstanding fetch reserves a queue slot. IF owns its response until
    // publication; admission counts this owned IF response as a reserved slot.
    reg id_valid,skid_valid,ex_valid,wb_valid,wb_write;
    reg [31:0] id_pc,id_instruction,id_immediate;
    reg [31:0] skid_pc,skid_instruction,skid_immediate;
    reg [31:0] ex_pc,ex_instruction,ex_immediate,ex_a,ex_b;
    reg ex_branch_late1,ex_branch_late2;
    reg [4:0] ex_branch_load_owner;
    reg [31:0] wb_pc,wb_instruction,wb_data;
    reg [31:0] id_prediction,skid_prediction,ex_prediction;
    // Prediction ownership follows the fetched instruction, never the current
    // history at resolution. Only resolved non-faulting branches train.
    reg [7:0] if_direction_index,id_direction_index,skid_direction_index,ex_direction_index;
    reg [7:0] resolved_history;
    // {target-hit, original-direction, correlated-direction} stays with its owner.
    reg [2:0] if_choices,id_choices,skid_choices,ex_choices;
    // P2: metadata banks clear in parallel during the existing 32-cycle RF initialization.
    reg [4:0] prediction_clear_index;
    reg prediction_initialized;
    wire direction_fetch_valid,direction_train_valid;
    wire [1:0] chooser_fetch_value,chooser_train_value;
    wire [7:0] direction_fetch_banks,direction_train_banks;
    wire [3:0] chooser_fetch_banks,chooser_train_banks;
    (* ram_style = "distributed" *) reg [1:0] direction_counters [0:255];
    wire [7:0] fetch_direction_index=fetch_owner_pc[9:2]^resolved_history;
    reg [7:0] mem_direction_index;
    reg [2:0] mem_choices;
    reg ex_prediction_sequential_match,ex_prediction_direct_match;
    reg [4:0] wb_address;
    reg multiply_started;
    reg [31:0] mem_csr_operand,wb_csr_operand;
    wire mem_available;
    wire mem_access_fault;
    // Fault metadata belongs to the same EX->MEM owner as address and PC.
    // Successful accesses cross the original boundary without an extra wait.
    reg mem_data_misaligned,mem_data_denied;
    wire mem_data_fault_pending=mem_data_misaligned || mem_data_denied;
    reg mem_valid,mem_write,mem_is_load,mem_is_store;
    wire [31:0] mem_data_fault_cause=mem_data_misaligned ?
        (mem_is_store ? 32'd6 : 32'd4) : (mem_is_store ? 32'd7 : 32'd5);
    reg mem_is_branch,mem_branch_taken,mem_branch_mismatch;
    wire mem_branch_fault,mem_branch_trap,mem_branch_redirect;
    reg [31:0] mem_pc,mem_instruction,mem_address,mem_store_data,mem_result;
    reg [4:0] mem_rd;
    wire [2:0] mem_funct3=mem_instruction[14:12];
    wire [31:0] branch_local_shifted=local_load_bypass_data >> {mem_address[1:0],3'b000};
    reg [31:0] branch_local_value;
    always @* begin
        case(mem_funct3)
            0:branch_local_value={{24{branch_local_shifted[7]}},branch_local_shifted[7:0]};
            1:branch_local_value={{16{branch_local_shifted[15]}},branch_local_shifted[15:0]};
            2:branch_local_value=local_load_bypass_data;
            4:branch_local_value={24'd0,branch_local_shifted[7:0]};
            5:branch_local_value={16'd0,branch_local_shifted[15:0]};
            default:branch_local_value=0;
        endcase
    end
    // This MEM-owned local RAM value serves late branch repair and the guarded
    // early low-MUL path. Ordinary ALU/address/CSR keep registered EX operands;
    // global DDR/MMIO/timer read muxes do not feed early local-MUL operands.
    wire [31:0] branch_a=ex_branch_late1 ? branch_local_value : ex_a;
    wire [31:0] branch_b=ex_branch_late2 ? branch_local_value : ex_b;
    reg local_load_waited;
    wire rf_initialized;
    wire rf_ready=rf_initialized && prediction_initialized;
    wire [31:0] rf_a,rf_b,incoming_immediate,raw_fetch_immediate;
    wire [4:0] id_rs1=id_instruction[19:15],id_rs2=id_instruction[24:20];
    wire [4:0] ex_rd=ex_instruction[11:7];
    wire [6:0] opcode=ex_instruction[6:0],funct7=ex_instruction[31:25];
    wire [2:0] funct3=ex_instruction[14:12];
    reg is_lui,is_auipc,is_imm,is_op,is_branch,is_jal,is_jalr,is_load,is_store,is_multiply;
    function [9:0] decode_instruction_class;
        input [31:0] instruction;
        reg [6:0] opcode,funct7;
        reg [2:0] funct3;
        reg is_lui,is_auipc,is_imm,is_op,is_branch,is_jal,is_jalr,is_load,is_store,is_multiply;
        begin
            opcode=instruction[6:0];funct7=instruction[31:25];funct3=instruction[14:12];
            is_lui=opcode==7'h37;is_auipc=opcode==7'h17;
            is_imm=opcode==7'h13;is_op=opcode==7'h33;
            is_branch=opcode==7'h63;is_jal=opcode==7'h6f;
            is_jalr=opcode==7'h67 && funct3==0;
            is_load=opcode==7'h03 && (funct3==0 || funct3==1 || funct3==2 || funct3==4 || funct3==5);
            is_store=opcode==7'h23 && funct3<3;
            is_multiply=is_op && funct7==1 && !funct3[2];
            decode_instruction_class={is_multiply,is_store,is_load,is_jalr,is_jal,is_branch,is_op,is_imm,is_auipc,is_lui};
        end
    endfunction
    wire [31:0] memory_address=ex_a+ex_immediate;
    assign data_permission_address=memory_address;
    assign data_permission_write=is_store;
    wire [31:0] multiply_result;
    wire multiply_done;
    wire multiply_execute_start=reset_n && rf_ready && ex_valid && is_multiply && !multiply_started && mem_available && !mem_branch_redirect;
    wire multiply_decode_start,multiply_decode_payload;
    // Class metadata follows the same owned instruction through all ID inputs.
    reg id_low_multiply,skid_low_multiply;
    function decode_low_multiply;
        input [31:0] instruction;
        begin decode_low_multiply=(instruction & 32'hfe00707f)==32'h02000033;end
    endfunction
    wire [31:0] multiply_decode_a,multiply_decode_b;
    wire multiply_start=multiply_execute_start || multiply_decode_start;
    rv32mul_pipe #(.FAST_LOW(1),.REGISTER_INPUT(1)) u_multiply(
        .clk(clk),.reset_n(reset_n),.start(multiply_start),.op(multiply_decode_payload ? 2'd0 : funct3[1:0]),
        .lhs(multiply_decode_payload ? multiply_decode_a : ex_a),.rhs(multiply_decode_payload ? multiply_decode_b : ex_b),.done(multiply_done),.result(multiply_result));
    rv32i_imm_gen u_incoming_immediate(.instruction(if_instruction),.immediate(incoming_immediate));
    rv32i_imm_gen u_raw_immediate(.instruction(instruction_data),.immediate(raw_fetch_immediate));

    reg [3:0] alu_operation;
    reg alu_valid;
    reg [31:0] alu_a,alu_b;
    wire [31:0] alu_result;
    function [4:0] decode_alu_control;
        input [31:0] instruction;
        reg [6:0] opcode,funct7;
        reg [2:0] funct3;
        reg is_lui,is_auipc,is_imm,is_op,is_multiply;
        reg [3:0] alu_operation;
        reg alu_valid;
        begin
        opcode=instruction[6:0];funct7=instruction[31:25];funct3=instruction[14:12];
        is_lui=opcode==7'h37;is_auipc=opcode==7'h17;
        is_imm=opcode==7'h13;is_op=opcode==7'h33;
        is_multiply=is_op && funct7==1 && !funct3[2];
        alu_operation=0;alu_valid=0;
        if(is_lui || is_auipc) begin
            alu_valid=1;
        end else if(is_imm || (is_op && !is_multiply)) begin
            case(funct3)
                0: begin
                    if(is_imm || funct7==0) begin alu_operation=0;alu_valid=1;end
                    else if(funct7==7'h20) begin alu_operation=1;alu_valid=1;end
                end
                1: if(funct7==0) begin alu_operation=7;alu_valid=1;end
                2: if(is_imm || funct7==0) begin alu_operation=5;alu_valid=1;end
                3: if(is_imm || funct7==0) begin alu_operation=6;alu_valid=1;end
                4: if(is_imm || funct7==0) begin alu_operation=4;alu_valid=1;end
                5: begin
                    if(funct7==0) begin alu_operation=8;alu_valid=1;end
                    else if(funct7==7'h20) begin alu_operation=9;alu_valid=1;end
                end
                6: if(is_imm || funct7==0) begin alu_operation=3;alu_valid=1;end
                7: if(is_imm || funct7==0) begin alu_operation=2;alu_valid=1;end
            endcase
        end
        decode_alu_control={alu_valid,alu_operation};
        end
    endfunction
    always @* begin
        alu_a=is_lui ? 0 : is_auipc ? ex_pc : ex_a;
        alu_b=(is_lui || is_auipc || is_imm) ? ex_immediate : ex_b;
    end
    rv32i_alu u_alu(.operand_a(alu_a),.operand_b(alu_b),.operation(alu_operation),.result(alu_result));
    function can_pair_alu;
        input [31:0] first,second;
        reg [4:0] first_control,second_control;
        reg second_reads1,second_reads2;
        reg [9:0] first_class;
        reg [4:0] first_destination;
        reg first_legal;
        begin
            first_control=decode_alu_control(first);second_control=decode_alu_control(second);
            first_class=decode_instruction_class(first);
            first_legal=first_control[4] || (WIDE_FIRST_SLOT && |first_class[9:7]);
            // Store bits[11:7] are an immediate, never a register destination.
            first_destination=first_class[8] ? 5'd0 : first[11:7];
            second_reads1=second[6:0]==7'h13 || second[6:0]==7'h33;
            second_reads2=second[6:0]==7'h33;
            can_pair_alu=first_legal && second_control[4] &&
                !(first_destination!=0 && ((second_reads1 && second[19:15]==first_destination) ||
                                           (second_reads2 && second[24:20]==first_destination))) &&
                (first_destination==0 || second[11:7]==0 || first_destination!=second[11:7]);
        end
    endfunction
    wire fetch_pair=ALIGNED_DUAL_ALU && instruction_pair_valid && !instruction_access_error &&
        fetch_owner_pc[1:0]==0 && fetch_owner_pc[5:2]!=4'hf && can_pair_alu(instruction_data,instruction_pair_data);
    wire [4:0] id_second_control=decode_alu_control(id_second_instruction);
    wire [31:0] ex_second_pc=ex_pc+32'd4;
    wire second_lui=ex_second_instruction[6:0]==7'h37;
    wire second_auipc=ex_second_instruction[6:0]==7'h17;
    wire second_immediate=second_lui || second_auipc || ex_second_instruction[6:0]==7'h13;
    rv32i_imm_gen u_second_immediate(.instruction(id_second_instruction),.immediate(id_second_immediate));
    rv32i_alu u_second_alu(.operand_a(second_lui ? 32'd0 : second_auipc ? ex_second_pc : ex_second_a),
        .operand_b(second_immediate ? ex_second_immediate : ex_second_b),
        .operation(ex_second_operation),.result(ex_second_result));
    reg branch_taken;
    always @* begin
        branch_taken=0;
        case(funct3)
            0:branch_taken=branch_a==branch_b;
            1:branch_taken=branch_a!=branch_b;
            4:branch_taken=$signed(branch_a)<$signed(branch_b);
            5:branch_taken=$signed(branch_a)>=$signed(branch_b);
            6:branch_taken=branch_a<branch_b;
            7:branch_taken=branch_a>=branch_b;
        endcase
    end
    wire [31:0] shifted_load=data_read_data >> {mem_address[1:0],3'b000};
    reg [31:0] load_result;
    always @* begin
        case(mem_funct3)
            0:load_result={{24{shifted_load[7]}},shifted_load[7:0]};
            1:load_result={{16{shifted_load[15]}},shifted_load[15:0]};
            2:load_result=data_read_data;
            4:load_result={24'd0,shifted_load[7:0]};
            5:load_result={16'd0,shifted_load[15:0]};
            default:load_result=0;
        endcase
    end
    wire ex_complete;
    wire is_system=opcode==7'h73;
    wire is_csr=is_system && funct3[1:0]!=0;
    wire is_mret=ex_instruction==32'h30200073;
    wire is_ecall=ex_instruction==32'h00000073;
    wire is_ebreak=ex_instruction==32'h00100073;
    wire wb_csr=wb_valid && wb_instruction[6:0]==7'h73 && wb_instruction[13:12]!=0;
    wire [31:0] selected_csr_instruction=wb_csr ? wb_instruction : ex_instruction;
    wire [31:0] ex_csr_operand=funct3[2] ? {27'd0,ex_instruction[19:15]} : ex_a;
    wire [31:0] csr_result,machine_vector,machine_irq_vector,machine_return;
    wire csr_illegal,machine_pending;
    wire [31:0] machine_cause;
    wire side_effects_idle=!DRAIN_PERIPHERAL_WRITES || peripheral_quiescent;
    wire irq_take=side_effects_idle && reset_n && rf_ready && machine_pending && !ex_valid && !mem_valid && !wb_valid;
    wire [31:0] irq_resume_pc=id_valid ? id_pc : skid_valid ? skid_pc :
        if_valid ? if_pc : (port_owner==PORT_FETCH && !fetch_killed) ? fetch_owner_pc : fetch_pc;
    wire system_trap=is_system && (is_ecall || is_ebreak ||
        (!is_csr && !is_mret) || (is_csr && csr_illegal));
    wire valid_branch=is_branch && funct3!=2 && funct3!=3;
    wire valid_fence=opcode==7'h0f && funct3==0;
    wire illegal_instruction=!(alu_valid || is_load || is_store || is_multiply ||
        is_jal || is_jalr || valid_branch || is_system || valid_fence);
    wire memory_misaligned=(is_load || is_store) &&
        ((funct3[1:0]==1 && memory_address[0]) || (funct3[1:0]==2 && |memory_address[1:0]));
    wire [31:0] control_target=is_jalr ? {memory_address[31:1],1'b0} : ex_pc+ex_immediate;
    wire control_misaligned=(is_jal || is_jalr || (valid_branch && branch_taken && !MEM_BRANCH_RESOLVE)) && control_target[1];
    wire memory_permission_trap=(is_load || is_store) && !data_access_permitted;
    wire fetch_permission_trap;
    wire execute_trap=ex_access_error || system_trap || illegal_instruction || control_misaligned;
    wire [31:0] execute_cause=ex_access_error ? 32'd1 : system_trap ? (is_ecall ? 32'd11 : is_ebreak ? 32'd3 : 32'd2) :
        illegal_instruction ? 32'd2 : control_misaligned ? 32'd0 : memory_misaligned ? (is_store ? 32'd6 : 32'd4) : is_store ? 32'd7 : 32'd5;
    wire [31:0] execute_tval=ex_access_error ? ex_pc : system_trap ? ((is_ecall || is_ebreak) ? 32'd0 : ex_instruction) :
        illegal_instruction ? ex_instruction : control_misaligned ? control_target : memory_address;
    rv32_machine_control #(.CSR_PROBE_ENABLE(1),.DUAL_RETIRE(ALIGNED_DUAL_ALU)) u_machine(
        .clk(clk),.reset_n(reset_n),.retire_valid(retire_valid),.retire_second_valid(retire_second_valid),.platform_time(platform_time),
        .csr_access(wb_csr || (ex_valid && is_csr)),
        .csr_commit(reset_n && rf_ready && wb_csr),
        .csr_write(selected_csr_instruction[13:12]==1 || selected_csr_instruction[19:15]!=0),
        .csr_address(selected_csr_instruction[31:20]),.csr_command(selected_csr_instruction[13:12]),
        .csr_operand(wb_csr ? wb_csr_operand : ex_csr_operand),
        .csr_read_data(csr_result),.csr_illegal(),
        .csr_probe_access(ex_valid && is_csr),
        .csr_probe_write(funct3[1:0]==1 || ex_instruction[19:15]!=0),
        .csr_probe_address(ex_instruction[31:20]),.csr_probe_command(funct3[1:0]),
        .csr_probe_illegal(csr_illegal),
        .trap_enter(irq_take || mem_access_fault || mem_branch_trap || fetch_permission_trap || (ex_complete && execute_trap)),.trap_pc(irq_take ? irq_resume_pc : (mem_access_fault || mem_branch_trap) ? mem_pc : fetch_permission_trap ? fetch_pc : ex_pc),
        .trap_cause(irq_take ? machine_cause : mem_access_fault ? mem_data_fault_cause : mem_branch_trap ? 32'd0 : fetch_permission_trap ? 32'd1 : execute_cause),
        .trap_value(irq_take ? 32'd0 : (mem_access_fault || mem_branch_trap) ? mem_address : fetch_permission_trap ? fetch_pc : execute_tval),
        .mret_commit(reset_n && rf_ready && wb_valid && wb_instruction==32'h30200073),
        .irq_external(irq_external),.irq_software(irq_software),.irq_timer(irq_timer),
        .interrupt_pending(machine_pending),.interrupt_cause(machine_cause),
        .trap_vector(),.exception_vector(machine_vector),
        .interrupt_vector(machine_irq_vector),.return_pc(machine_return));
    wire local_load=OVERLAP_LOCAL_LOAD && mem_is_load && !data_address_is_ddr;
    wire load_demand=mem_valid && mem_is_load && !mem_data_fault_pending && !local_load && side_effects_idle;
    wire load_complete=local_load ? (local_load_waited && data_read_ready) :
        (port_owner==PORT_LOAD && load_started && data_read_ready);
    assign mem_access_fault=reset_n && rf_ready && mem_valid &&
        (mem_data_fault_pending ? (!wb_valid && side_effects_idle) :
         (mem_is_load && load_complete && data_access_error));
    wire mem_complete=reset_n && rf_ready && mem_valid &&
        (mem_data_fault_pending ? (!wb_valid && side_effects_idle) :
         mem_is_load ? load_complete : mem_is_store ? data_write_ready : 1'b1) &&
        (!mem_branch_fault || (!wb_valid && side_effects_idle));
    // Compare/target payload crossed EX->MEM before any global recovery action.
    assign mem_branch_fault=MEM_BRANCH_RESOLVE && mem_valid && mem_is_branch && mem_branch_taken && mem_address[1];
    assign mem_branch_trap=mem_complete && mem_branch_fault;
    assign mem_branch_redirect=MEM_BRANCH_RESOLVE && mem_complete && mem_is_branch && (mem_branch_fault || mem_branch_mismatch);
    assign mem_available=!mem_valid || mem_complete;
    wire [31:0] mem_writeback=mem_is_load ? load_result : mem_result;
    assign ex_complete=reset_n && rf_ready && ex_valid && mem_available && !mem_access_fault && !mem_branch_redirect &&
        ((is_system || execute_trap || valid_fence) ? (!mem_valid && !wb_valid && side_effects_idle) : (!is_multiply || (multiply_started && multiply_done)));
    wire ex_write=(alu_valid || is_jal || is_jalr || is_load || is_multiply || is_csr) && ex_rd!=0 && !execute_trap;
    wire [31:0] ex_result=is_csr ? csr_result : is_multiply ? multiply_result :
        (is_jal || is_jalr) ? ex_pc+4 : alu_result;
    wire actual_taken=is_jal || is_jalr || (is_branch && branch_taken && !MEM_BRANCH_RESOLVE);
    wire [31:0] actual_next_pc=execute_trap ? machine_vector : is_system ? (is_mret ? machine_return : ex_pc+4) : actual_taken ?
        (is_jalr ? {memory_address[31:1],1'b0} : ex_pc+ex_immediate) : ex_pc+(ex_pair ? 32'd8 : 32'd4);
    // Direct target and sequential matches are independent of forwarded operands.
    // Register them at issue so the branch comparison only selects one bit.
    // JALR retains its operand-dependent target comparison in execute.
    // Traps and MRET redirect unconditionally; their vector is payload, not admission control.
    wire prediction_mismatch=is_jalr ? ({memory_address[31:1],1'b0}!=ex_prediction) :
        actual_taken ? !ex_prediction_direct_match : !ex_prediction_sequential_match;
    wire redirect=irq_take || mem_access_fault || mem_branch_redirect || fetch_permission_trap || (ex_complete && (execute_trap || is_mret || (!(MEM_BRANCH_RESOLVE && is_branch) && (STATIC_CONTROL_PREDICTION ? prediction_mismatch : actual_taken))));
    wire [31:0] redirect_pc=irq_take ? machine_irq_vector : (mem_access_fault || mem_branch_trap || fetch_permission_trap) ? machine_vector : mem_branch_redirect ? (mem_branch_taken ? mem_address : mem_result) : actual_next_pc;
    wire ex_available=!ex_valid || ex_complete;
    wire [9:0] id_class_for_hint=decode_instruction_class(id_instruction);
    wire id_reads_rs1=id_class_for_hint[2] || id_class_for_hint[3] || id_class_for_hint[4] ||
        id_class_for_hint[6] || id_class_for_hint[7] || id_class_for_hint[8] ||
        (id_instruction[6:0]==7'h73 && !id_instruction[14] && id_instruction[13:12]!=0);
    wire id_reads_rs2=id_class_for_hint[3] || id_class_for_hint[4] || id_class_for_hint[8];
    wire id_second_reads1=id_second_instruction[6:0]==7'h13 || id_second_instruction[6:0]==7'h33;
    wire id_second_reads2=id_second_instruction[6:0]==7'h33;
    wire [3:0] id_read_mask={id_pair && id_second_reads2,id_pair && id_second_reads1,id_reads_rs2,id_reads_rs1};
    wire [19:0] id_read_addresses={id_second_rs2,id_second_rs1,id_rs2,id_rs1};
    function automatic id_consumes;
        input [4:0] rd;
        input [3:0] reads;
        input [19:0] addresses;
        integer n;
        begin
            id_consumes=0;
            for(n=0;n<4;n=n+1) if(rd!=0 && reads[n] && addresses[n*5+:5]==rd) id_consumes=1;
        end
    endfunction
    wire ex_primary_dependency=ex_valid && ex_write && id_consumes(ex_rd,id_read_mask,id_read_addresses);
    wire ex_secondary_dependency=ex_valid && ex_pair && id_consumes(ex_second_rd,id_read_mask,id_read_addresses);
    wire ex_dependency=ex_primary_dependency || ex_secondary_dependency;
    wire mem_dependency=mem_valid && ((mem_write && id_consumes(mem_rd,id_read_mask,id_read_addresses)) ||
        (mem_pair && id_consumes(mem_second_rd,id_read_mask,id_read_addresses)));
    wire branch_late_admit=MEM_BRANCH_RESOLVE && EX_RESULT_BYPASS &&
        id_class_for_hint[4] && id_instruction[14:13]!=2'b01 &&
        local_read_hint && local_read_hint_ready;
    // A first-slot load does not delay its independent second ALU result.
    // Actual issue still requires EX completion; the existing two-producer
    // bypass then supplies the second result. Primary load uses still wait.
    wire dependency_wait=(ex_dependency && !EX_RESULT_BYPASS) ||
        (ex_primary_dependency && is_load && !branch_late_admit) ||
        (mem_dependency && !mem_complete);
    wire id_issue=reset_n && rf_ready && id_valid && ex_available && !redirect && !dependency_wait &&
        !machine_pending && !(ex_valid && (is_system || execute_trap || valid_fence)) &&
        !(mem_valid && mem_instruction[6:0]==7'h73) && !(wb_valid && wb_instruction[6:0]==7'h73);
    wire register_write=reset_n && rf_ready && wb_valid && wb_write;
    wire register_write_second=retire_second_valid && wb_second_rd!=0;
    wire forward_ex1=EX_RESULT_BYPASS && !is_load && ex_complete && ex_write && ex_rd==id_rs1 && id_rs1!=0;
    wire forward_ex2=EX_RESULT_BYPASS && !is_load && ex_complete && ex_write && ex_rd==id_rs2 && id_rs2!=0;
    wire forward_wb1=register_write && wb_address==id_rs1 && id_rs1!=0;
    wire forward_wb2=register_write && wb_address==id_rs2 && id_rs2!=0;
    wire forward_mem1=mem_complete && mem_write && mem_rd==id_rs1 && id_rs1!=0;
    wire forward_mem2=mem_complete && mem_write && mem_rd==id_rs2 && id_rs2!=0;
    // Age priority spans both producers: EX before MEM before WB. Pair lanes
    // have distinct nonzero destinations, but adjacent groups may overwrite them.
    wire [5:0] bypass_valid={register_write,register_write_second,
        mem_complete && mem_write,mem_complete && mem_pair,
        EX_RESULT_BYPASS && !is_load && ex_complete && ex_write,
        EX_RESULT_BYPASS && ex_complete && ex_pair};
    wire [29:0] bypass_addresses={wb_address,wb_second_rd,mem_rd,mem_second_rd,ex_rd,ex_second_rd};
    wire [191:0] bypass_values={wb_data,wb_second_data,mem_writeback,mem_second_result,ex_result,ex_second_result};
    // Every changing dependency is an explicit argument. Hidden global reads
    // inside a continuous-assignment helper can leave simulation values stale.
    function automatic [31:0] forwarded_operand;
        input [4:0] rs;
        input [31:0] rf_value;
        input [5:0] producers;
        input [29:0] destinations;
        input [191:0] values;
        integer n;
        reg found;
        begin
            forwarded_operand=rs==0 ? 32'd0 : rf_value;found=(rs==0);
            for(n=0;n<6;n=n+1) begin
                if(!found && producers[n] && destinations[n*5+:5]==rs) begin
                    forwarded_operand=values[n*32+:32];found=1;
                end
            end
        end
    endfunction
    wire [31:0] decode_a=forwarded_operand(id_rs1,rf_a,bypass_valid,bypass_addresses,bypass_values);
    wire [31:0] decode_b=forwarded_operand(id_rs2,rf_b,bypass_valid,bypass_addresses,bypass_values);
    wire [31:0] decode_second_a=forwarded_operand(id_second_rs1,rf_c,bypass_valid,bypass_addresses,bypass_values);
    wire [31:0] decode_second_b=forwarded_operand(id_second_rs2,rf_d,bypass_valid,bypass_addresses,bypass_values);
    // Only a registered local work-RAM load owner may supply the early lane.
    // Payload selection excludes ready/redirect logic; actual id_issue plus
    // mem_available still requires completion and rejects traps before start.
    wire multiply_mem_load=EARLY_MUL_LOCAL_LOAD && mem_valid && local_load && local_load_waited &&
        mem_address[31:17]==15'h1000 && !mem_data_fault_pending;
    wire multiply_load_a=multiply_mem_load && mem_write && id_rs1!=0 && mem_rd==id_rs1;
    wire multiply_load_b=multiply_mem_load && mem_write && id_rs2!=0 && mem_rd==id_rs2;
    // The early MUL mux contains the primary local-load result, not the MEM
    // second producer. An unshadowed MEM-second source retains normal EX fallback.
    wire multiply_mem_second_dependency=mem_valid && mem_pair && mem_second_rd!=0 &&
        (mem_second_rd==id_rs1 || mem_second_rd==id_rs2);
    // Ordinary EX ALU/link payload now ends at the already existing input FFs.
    // Acceptance still uses id_issue, so fault/redirect and older MEM ordering
    // remain unchanged. Exclude load/MUL/CSR payloads from this new early path.
    wire multiply_ex_forwardable=!is_load && !is_multiply && !is_csr &&
        (alu_valid || is_jal || is_jalr || ex_pair);
    wire multiply_ex_primary=ex_valid && (alu_valid || is_jal || is_jalr) &&
        !is_multiply && !is_csr && ex_rd!=0;
    wire [31:0] multiply_ex_value=(is_jal || is_jalr) ? ex_pc+32'd4 : alu_result;
    wire multiply_ex_a=multiply_ex_primary && id_rs1!=0 && ex_rd==id_rs1;
    wire multiply_ex_b=multiply_ex_primary && id_rs2!=0 && ex_rd==id_rs2;
    wire multiply_ex_second_a=ex_valid && ex_pair && id_rs1!=0 && ex_second_rd==id_rs1;
    wire multiply_ex_second_b=ex_valid && ex_pair && id_rs2!=0 && ex_second_rd==id_rs2;
    // Readiness belongs to each MUL operand's youngest producer. A newer
    // ordinary EX producer shadows an older MEM write to that same register.
    // Unshadowed MEM operands still need the already supported local-load path.
    wire multiply_mem_uses_a=mem_valid && id_rs1!=0 &&
        ((mem_write && mem_rd==id_rs1) || (mem_pair && mem_second_rd==id_rs1));
    wire multiply_mem_uses_b=mem_valid && id_rs2!=0 &&
        ((mem_write && mem_rd==id_rs2) || (mem_pair && mem_second_rd==id_rs2));
    wire multiply_early_a_ready=multiply_ex_a || multiply_ex_second_a || multiply_load_a || !multiply_mem_uses_a;
    wire multiply_early_b_ready=multiply_ex_b || multiply_ex_second_b || multiply_load_b || !multiply_mem_uses_b;
    assign multiply_decode_start=ID_INDEPENDENT_LOW_MUL && id_issue && id_low_multiply && !id_access_error && mem_available &&
        !(ex_valid && (is_load || is_store)) && (!ex_dependency || multiply_ex_forwardable) &&
        multiply_early_a_ready && multiply_early_b_ready;
    // Payload may be speculative; only multiply_start updates arithmetic state.
    // An unstarted EX multiply must keep its own operands/op, even with a low
    // multiply waiting in ID. Otherwise a decode launch may select ID data
    // without feeding the long EX-permission/redirect/issue cone into the DSP.
    assign multiply_decode_payload=ID_INDEPENDENT_LOW_MUL && id_valid && id_low_multiply && (!ex_valid || !is_multiply || multiply_started);
    assign multiply_decode_a=multiply_ex_second_a ? ex_second_result :
        multiply_ex_a ? multiply_ex_value : multiply_load_a ? branch_local_value : register_write_second && wb_second_rd==id_rs1 && id_rs1!=0 ? wb_second_data : forward_wb1 ? wb_data : rf_a;
    assign multiply_decode_b=multiply_ex_second_b ? ex_second_result :
        multiply_ex_b ? multiply_ex_value : multiply_load_b ? branch_local_value : register_write_second && wb_second_rd==id_rs2 && id_rs2!=0 ? wb_second_data : forward_wb2 ? wb_data : rf_b;
    // EX computes the exact next MEM address. Port B may read it early;
    // port A remains owned by the older MEM load during backpressure.
    assign local_read_address=memory_address;
    // Port-B preview reads only local RAM and has no architectural side effect.
    // A denied/misaligned load is discarded in MEM before its public request.
    assign local_read_hint=PREFETCH_LOCAL_RAM && reset_n && rf_ready && ex_valid && is_load && !execute_trap &&
        (SEPARATE_LOCAL_HINT_PORT || !mem_valid || !mem_is_load) &&
        memory_address[31:17]==15'h1000;
    generate if(ALIGNED_DUAL_ALU) begin:g_dual_rf
        rv32i_banked_regfile #(.SYNC_RESET(SYNC_RESET)) u_regfile(
            .clk(clk),.reset_n(reset_n),.ready(rf_initialized),.write_pair_ready(rf_pair_ready),
            .read_addr0(id_rs1),.read_addr1(id_rs2),.read_addr2(id_second_rs1),.read_addr3(id_second_rs2),
            .read_data0(rf_a),.read_data1(rf_b),.read_data2(rf_c),.read_data3(rf_d),
            .write_enable0(register_write),.write_addr0(wb_address),.write_data0(wb_data),
            .write_enable1(register_write_second),.write_addr1(wb_second_rd),.write_data1(wb_second_data));
    end else begin:g_scalar_rf
        assign rf_pair_ready=rf_ready;assign rf_c=0;assign rf_d=0;
        rv32i_serial_regfile #(.SYNC_RESET(SYNC_RESET)) u_regfile(
            .clk(clk),.reset_n(reset_n),.ready(rf_initialized),
            .read_addr1(id_rs1),.read_addr2(id_rs2),.read_data1(rf_a),.read_data2(rf_b),
            .write_enable(register_write),.write_addr(wb_address),.write_data(wb_data));
    end endgenerate

    wire fetch_owner_allowed=!CHECK_FETCH_PERMISSION || instruction_permitted;
    wire fetch_next_allowed=!CHECK_FETCH_PERMISSION ||
        (SEPARATE_FETCH_PERMISSION ? instruction_next_permitted : instruction_permitted);
    // Only observation selects by owner; request admission uses its dedicated result.
    wire fetch_allowed=port_owner==PORT_FETCH ? fetch_owner_allowed : fetch_next_allowed;
    wire fetch_age_ready=fetch_owner_pc[31:24]==8'h80 || fetch_age>=LOCAL_FETCH_WAIT;
    wire fetch_response=port_owner==PORT_FETCH && fetch_age_ready && instruction_ready && (fetch_owner_allowed || fetch_admitted);
    // Both normal redirect origins may reuse an already completed old fetch.
    // MEM redirect suppresses ex_complete, so it has its own precise-trap guard.
    wire redirect_response_handoff=REDIRECT_RESPONSE_HANDOFF && redirect &&
        ((mem_branch_redirect && !mem_branch_trap) ||
         (ex_complete && !execute_trap && !is_mret)) &&
        !irq_take && !mem_access_fault && !fetch_permission_trap &&
        !machine_pending && !load_demand && fetch_response && !fetch_killed;
    // Empty queues permit one owned normal response to populate registered ID.
    // Pending interrupts, killed responses, errors and redirects retain the IF path.
    wire empty_response_to_id=EMPTY_FETCH_BYPASS && !if_valid && (!id_valid || id_issue) && !skid_valid &&
        fetch_response && !fetch_killed && !redirect && !machine_pending && !instruction_access_error;
    wire [2:0] queued_after_issue={2'b0,id_valid}+{2'b0,skid_valid}-{2'b0,id_issue};
    assign instruction_address=port_owner==PORT_FETCH ? fetch_owner_pc : fetch_pc;
    assign instruction_request=reset_n && rf_ready && port_owner==PORT_FETCH && (fetch_owner_allowed || fetch_admitted);
    // Check the current request PC, outside the prediction-to-next-owner enable path.
    // Once issued, a delayed request remains owned until its response or reset/HOLD.
    wire predict_taken=STATIC_CONTROL_PREDICTION &&
        (instruction_data[6:0]==7'h6f ||
         (instruction_data[6:0]==7'h63 && raw_fetch_immediate[31]));
    // Last resolved JALR target, fully tagged to reject direct-map aliases.
    // Valid bits clear on every reset/host HOLD; tags/targets need no reset mux.
    reg [31:0] indirect_valid;
    (* ram_style = "distributed" *) reg [24:0] indirect_tags [0:31];
    (* ram_style = "distributed" *) reg [30:0] indirect_targets [0:31];
    wire indirect_prediction_hit=INDIRECT_TARGET_CACHE && STATIC_CONTROL_PREDICTION &&
        instruction_data[6:0]==7'h67 && instruction_data[14:12]==0 &&
        indirect_valid[fetch_owner_pc[6:2]] && indirect_tags[fetch_owner_pc[6:2]]==fetch_owner_pc[31:7];
    // Resolved conditional branches train a tagged saturating direction counter.
    // Store the direct target so fetch does not add a decoded immediate on hits.
    reg [63:0] branch_history_valid;
    (* ram_style = "distributed" *) reg [23:0] branch_history_tags [0:63];
    (* ram_style = "distributed" *) reg [31:0] branch_history_targets [0:63];
    (* ram_style = "distributed" *) reg [1:0] branch_history_counters [0:63];
    wire branch_history_hit=DYNAMIC_BRANCH_PREDICTION && STATIC_CONTROL_PREDICTION &&
        instruction_data[6:0]==7'h63 && instruction_data[14:13]!=2'b01 &&
        branch_history_valid[fetch_owner_pc[7:2]] &&
        branch_history_tags[fetch_owner_pc[7:2]]==fetch_owner_pc[31:8];
    wire [31:0] branch_resolution_pc=MEM_BRANCH_RESOLVE ? mem_pc : ex_pc;
    wire [31:0] branch_resolution_target=MEM_BRANCH_RESOLVE ? mem_address : ex_pc+ex_immediate;
    wire branch_resolution_taken=MEM_BRANCH_RESOLVE ? mem_branch_taken : branch_taken;
    wire branch_history_update=reset_n && rf_ready && DYNAMIC_BRANCH_PREDICTION &&
        (MEM_BRANCH_RESOLVE ? (mem_complete && mem_is_branch) : (ex_complete && is_branch && funct3[2:1]!=2'b01));
    wire branch_history_existing=branch_history_valid[branch_resolution_pc[7:2]] &&
        branch_history_tags[branch_resolution_pc[7:2]]==branch_resolution_pc[31:8];
    wire [1:0] branch_history_old=branch_history_counters[branch_resolution_pc[7:2]];
    wire [1:0] branch_history_next=!branch_history_existing ? (branch_resolution_taken ? 2'd2 : 2'd1) :
        branch_resolution_taken ? (branch_history_old==3 ? 2'd3 : branch_history_old+2'd1) :
        (branch_history_old==0 ? 2'd0 : branch_history_old-2'd1);
    wire [7:0] branch_resolution_direction_index=MEM_BRANCH_RESOLVE ? mem_direction_index : ex_direction_index;
    wire [2:0] branch_resolution_choices=MEM_BRANCH_RESOLVE ? mem_choices : ex_choices;
    wire direction_update=CORRELATED_DIRECTION && branch_history_update && !(MEM_BRANCH_RESOLVE ? mem_branch_trap : execute_trap);
    wire [1:0] chooser_old=chooser_train_value;
    wire chooser_update=direction_update && branch_resolution_choices[2] && (branch_resolution_choices[1]!=branch_resolution_choices[0]);
    wire [1:0] chooser_next=branch_resolution_choices[0]==branch_resolution_taken ?
        (chooser_old==3 ? 2'd3 : chooser_old+2'd1) :
        (chooser_old==0 ? 2'd0 : chooser_old-2'd1);
    wire [1:0] direction_old=direction_counters[branch_resolution_direction_index];
    wire [1:0] direction_next=!direction_train_valid ? (branch_resolution_taken ? 2'd2 : 2'd1) :
        branch_resolution_taken ? (direction_old==3 ? 2'd3 : direction_old+2'd1) :
        (direction_old==0 ? 2'd0 : direction_old-2'd1);
    wire correlated_prediction=CORRELATED_DIRECTION && direction_fetch_valid ?
        direction_counters[fetch_direction_index][1] : branch_history_counters[fetch_owner_pc[7:2]][1];
    wire base_prediction=branch_history_counters[fetch_owner_pc[7:2]][1];
    wire predicted_direction=chooser_fetch_value[1] ? correlated_prediction : base_prediction;
    // Payload RAMs have one synchronous write port and asynchronous lookup ports.
    // Initialization uses a local address counter, never a reset on RAM contents.
    task step_prediction_initialize;
    begin
        if(!reset_n) begin prediction_clear_index<=0;prediction_initialized<=0;end
        else if(!prediction_initialized) begin
            if(prediction_clear_index==31) prediction_initialized<=1;
            else prediction_clear_index<=prediction_clear_index+1'b1;
        end
    end
    endtask
    generate if(SYNC_RESET) begin:g_sync_prediction_init
        always @(posedge clk) step_prediction_initialize();
    end else begin:g_async_prediction_init
        always @(posedge clk or negedge reset_n) step_prediction_initialize();
    end endgenerate
    genvar direction_bank,chooser_bank;
    generate for(direction_bank=0;direction_bank<8;direction_bank=direction_bank+1) begin:g_direction_valid_bank
        localparam [2:0] BANK=direction_bank;
        (* ram_style="distributed" *) reg words [0:31];
        always @(posedge clk) if(reset_n) begin
            if(!prediction_initialized) words[prediction_clear_index]<=1'b0;
            else if(direction_update && branch_resolution_direction_index[7:5]==BANK)
                words[branch_resolution_direction_index[4:0]]<=1'b1;
        end
        assign direction_fetch_banks[direction_bank]=words[fetch_direction_index[4:0]];
        assign direction_train_banks[direction_bank]=words[branch_resolution_direction_index[4:0]];
    end endgenerate
    generate for(chooser_bank=0;chooser_bank<2;chooser_bank=chooser_bank+1) begin:g_direction_chooser_bank
        localparam BANK=chooser_bank;
        (* ram_style="distributed" *) reg [1:0] words [0:31];
        always @(posedge clk) if(reset_n) begin
            if(!prediction_initialized) words[prediction_clear_index]<=2'd0;
            else if(chooser_update && branch_resolution_pc[7]==BANK)
                words[branch_resolution_pc[6:2]]<=chooser_next;
        end
        assign chooser_fetch_banks[chooser_bank*2 +: 2]=words[fetch_owner_pc[6:2]];
        assign chooser_train_banks[chooser_bank*2 +: 2]=words[branch_resolution_pc[6:2]];
    end endgenerate
    assign direction_fetch_valid=prediction_initialized && direction_fetch_banks[fetch_direction_index[7:5]];
    assign direction_train_valid=prediction_initialized && direction_train_banks[branch_resolution_direction_index[7:5]];
    assign chooser_fetch_value=prediction_initialized ? chooser_fetch_banks[fetch_owner_pc[7]*2 +: 2] : 2'd0;
    assign chooser_train_value=prediction_initialized ? chooser_train_banks[branch_resolution_pc[7]*2 +: 2] : 2'd0;
`ifndef SYNTHESIS
    // Read-only logical views for existing observers; no production memory read ports.
    // Before initialization, all prediction entries are logically invalid/neutral.
    wire [255:0] direction_valid;
    wire [127:0] direction_chooser;
    genvar prediction_bit,chooser_entry;
    generate for(prediction_bit=0;prediction_bit<256;prediction_bit=prediction_bit+1) begin:g_prediction_observer
        assign direction_valid[prediction_bit]=prediction_initialized ?
            g_direction_valid_bank[prediction_bit/32].words[prediction_bit%32] : 1'b0;
    end endgenerate
    generate for(chooser_entry=0;chooser_entry<64;chooser_entry=chooser_entry+1) begin:g_chooser_observer
        assign direction_chooser[chooser_entry*2 +: 2]=prediction_initialized ?
            g_direction_chooser_bank[chooser_entry/32].words[chooser_entry%32] : 2'd0;
    end endgenerate
    always @(posedge clk) if(reset_n && rf_initialized!==prediction_initialized)
        $fatal(1,"P2_RF_PREDICTION_INITIALIZATION_BOUNDARY");
`endif
    always @(posedge clk) if(direction_update)
        direction_counters[branch_resolution_direction_index]<=direction_next;
    wire [31:0] incoming_prediction=fetch_pair ? fetch_owner_pc+32'd8 : branch_history_hit ?
        (predicted_direction ?
            branch_history_targets[fetch_owner_pc[7:2]] : fetch_owner_pc+32'd4) :
        indirect_prediction_hit ?
        {indirect_targets[fetch_owner_pc[6:2]],1'b0} :
        PREDECODED_PREDICTION ? instruction_prediction :
        fetch_owner_pc+(predict_taken ? raw_fetch_immediate : 32'd4);
    // Capture predictor state by PC without waiting for incoming instruction decode.
    // The next IF stage combines these owned snapshots with its registered opcode.
    wire capture_history_hit=DYNAMIC_BRANCH_PREDICTION && STATIC_CONTROL_PREDICTION &&
        branch_history_valid[fetch_owner_pc[7:2]] &&
        branch_history_tags[fetch_owner_pc[7:2]]==fetch_owner_pc[31:8];
    wire capture_indirect_hit=INDIRECT_TARGET_CACHE && STATIC_CONTROL_PREDICTION &&
        indirect_valid[fetch_owner_pc[6:2]] && indirect_tags[fetch_owner_pc[6:2]]==fetch_owner_pc[31:7];
    wire if_history_select=if_history_hit && if_instruction[6:0]==7'h63 && if_instruction[14:13]!=2'b01;
    wire if_indirect_select=if_indirect_hit && if_instruction[6:0]==7'h67 && if_instruction[14:12]==0;
    wire if_static_taken=STATIC_CONTROL_PREDICTION &&
        (if_instruction[6:0]==7'h6f || (if_instruction[6:0]==7'h63 && incoming_immediate[31]));
    wire [31:0] if_incoming_prediction=if_pair ? if_pc+32'd8 : if_history_select ?
        (if_history_taken ? if_history_target : if_pc+32'd4) :
        if_indirect_select ? {if_indirect_target,1'b0} :
        PREDECODED_PREDICTION ? if_prediction : if_pc+(if_static_taken ? incoming_immediate : 32'd4);
    assign instruction_permission_address=SEPARATE_FETCH_PERMISSION ? fetch_owner_pc : instruction_address;
    // An unresolved older branch may still redirect an invalid speculative PC.
    assign instruction_fault=fetch_permission_trap;
    assign fetch_permission_trap=side_effects_idle && CHECK_FETCH_PERMISSION && reset_n && rf_ready &&
        port_owner==PORT_IDLE && !if_valid && !id_valid && !skid_valid && !ex_valid && !mem_valid && !wb_valid && !machine_pending && !fetch_next_allowed;
    assign instruction_next_hint=0;
    assign instruction_next_address=fetch_pc;
    assign data_address=mem_address;
    assign data_address_is_ddr=mem_address[31:24]==8'h80;
    assign data_read_request=reset_n && rf_ready && !mem_data_fault_pending &&
        ((port_owner==PORT_LOAD && load_demand) || (mem_valid && local_load));
    assign data_write_data=mem_funct3==0 ? {4{mem_store_data[7:0]}} :
        mem_funct3==1 ? {2{mem_store_data[15:0]}} : mem_store_data;
    assign data_write_mask=reset_n && rf_ready && mem_valid && mem_is_store && !mem_data_fault_pending ?
        (mem_funct3==0 ? 4'b0001 << mem_address[1:0] :
         mem_funct3==1 ? (mem_address[1] ? 4'b1100 : 4'b0011) : 4'b1111) : 4'b0000;
    assign data_write_intent=|data_write_mask;
    assign data_write_strobe=data_write_mask & {4{data_write_ready}};
    assign data_write_enable=|data_write_strobe;
    assign debug_pc=ex_pc;
    assign debug_instruction=ex_instruction;
    assign debug_state=ex_valid ? 3'd2 : 3'd0;
    assign debug_reg_write=register_write;
    assign debug_reg_write_addr=wb_address;
    assign debug_reg_write_data=wb_data;
    assign retire_valid=reset_n && rf_ready && wb_valid;
    assign retire_pc=wb_pc;
    assign retire_instruction=wb_instruction;
    assign retire_second_valid=ALIGNED_DUAL_ALU && retire_valid && wb_pair;
    assign retire_second_pc=wb_pc+32'd4;
    assign retire_second_instruction=wb_second_instruction;
    assign retire_second_reg_write=register_write_second;
    assign retire_second_rd=wb_second_rd;
    assign retire_second_data=wb_second_data;

    // Payload RAM has a write enable, never an implicit reset/hold assignment.
    // Resettable valid bits in the control process hide old entries after reset.
    always @(posedge clk) begin
        if(reset_n && rf_ready && INDIRECT_TARGET_CACHE && ex_complete && is_jalr && funct3==0) begin
            indirect_tags[ex_pc[6:2]]<=ex_pc[31:7];
            indirect_targets[ex_pc[6:2]]<=actual_next_pc[31:1];
        end
    end
    always @(posedge clk) begin
        if(branch_history_update) begin
            branch_history_tags[branch_resolution_pc[7:2]]<=branch_resolution_pc[31:8];
            branch_history_targets[branch_resolution_pc[7:2]]<=branch_resolution_target;
            branch_history_counters[branch_resolution_pc[7:2]]<=branch_history_next;
        end
    end
    task step_pipeline;
    begin
        // Port B owns only the first cycle after an accepted EX load enters MEM.
        // On every later cycle Port A has read the current MEM address, even
        // when MEM or register readiness stalls. Reset/HOLD also clears ownership.
        local_load_hint_first<=0;
        if(!reset_n) begin
            port_owner<=PORT_IDLE;fetch_pc<=0;fetch_owner_pc<=0;fetch_age<=0;
            if_valid<=0;if_pc<=0;if_instruction<=32'h13;if_prediction<=0;
            if_pair<=0;id_pair<=0;skid_pair<=0;ex_pair<=0;mem_pair<=0;wb_pair<=0;
            if_second_instruction<=32'h13;id_second_instruction<=32'h13;skid_second_instruction<=32'h13;
            ex_second_instruction<=32'h13;mem_second_instruction<=32'h13;wb_second_instruction<=32'h13;
            ex_second_a<=0;ex_second_b<=0;ex_second_immediate<=0;ex_second_operation<=0;
            mem_second_result<=0;wb_second_data<=0;
            if_access_error<=0;id_access_error<=0;skid_access_error<=0;ex_access_error<=0;
            if_history_hit<=0;if_history_taken<=0;if_indirect_hit<=0;
            fetch_killed<=0;load_started<=0;multiply_started<=0;fetch_admitted<=0;
            indirect_valid<=0;branch_history_valid<=0;
            resolved_history<=0;
            if_choices<=0;id_choices<=0;skid_choices<=0;ex_choices<=0;mem_choices<=0;
            if_direction_index<=0;id_direction_index<=0;skid_direction_index<=0;ex_direction_index<=0;mem_direction_index<=0;
            id_valid<=0;skid_valid<=0;ex_valid<=0;wb_valid<=0;wb_write<=0;
            id_pc<=0;id_instruction<=32'h13;id_immediate<=0;
            id_low_multiply<=0;skid_low_multiply<=0;
            skid_pc<=0;skid_instruction<=32'h13;skid_immediate<=0;
            ex_pc<=0;ex_instruction<=32'h13;ex_immediate<=0;ex_a<=0;ex_b<=0;
            ex_branch_late1<=0;ex_branch_late2<=0;ex_branch_load_owner<=0;
            wb_pc<=0;wb_instruction<=32'h13;wb_data<=0;wb_address<=0;mem_csr_operand<=0;wb_csr_operand<=0;
            id_prediction<=0;skid_prediction<=0;ex_prediction<=0;local_load_waited<=0;
            ex_prediction_sequential_match<=0;ex_prediction_direct_match<=0;
            {is_multiply,is_store,is_load,is_jalr,is_jal,is_branch,is_op,is_imm,is_auipc,is_lui}<=10'd0;
            {alu_valid,alu_operation}<=5'd0;
            mem_valid<=0;mem_write<=0;mem_is_load<=0;mem_is_store<=0;
            mem_is_branch<=0;mem_branch_taken<=0;mem_branch_mismatch<=0;
            mem_data_misaligned<=0;mem_data_denied<=0;
            mem_pc<=0;mem_instruction<=32'h13;mem_address<=0;mem_store_data<=0;mem_result<=0;mem_rd<=0;
        end else if(rf_ready) begin
            if(branch_history_update) branch_history_valid[branch_resolution_pc[7:2]]<=1'b1;
            if(direction_update) begin
                resolved_history<={resolved_history[6:0],branch_resolution_taken};
            end
            if(INDIRECT_TARGET_CACHE && ex_complete && is_jalr && funct3==0) begin
                indirect_valid[ex_pc[6:2]]<=1'b1;
            end
            if(port_owner!=PORT_FETCH) fetch_admitted<=0;
            else if(instruction_request) fetch_admitted<=1;
            if(mem_available) local_load_waited<=0;
            else if(mem_valid && local_load) local_load_waited<=1;
            wb_valid<=mem_complete && !mem_access_fault && !mem_branch_trap;
            wb_pair<=mem_complete && !mem_access_fault && !mem_branch_trap && mem_pair;
            if(mem_complete) begin
                wb_pc<=mem_pc;wb_instruction<=mem_instruction;wb_csr_operand<=mem_csr_operand;
                wb_second_instruction<=mem_second_instruction;wb_second_data<=mem_second_result;
                wb_write<=mem_write;wb_address<=mem_rd;wb_data<=mem_writeback;
            end
            if(mem_available) begin
                mem_valid<=ex_complete && !execute_trap;
                mem_pair<=ex_complete && !execute_trap && ex_pair;
                mem_is_branch<=MEM_BRANCH_RESOLVE && ex_complete && valid_branch;
                if(ex_complete) begin
                    mem_data_misaligned<=memory_misaligned;
                    mem_data_denied<=memory_permission_trap;
                    mem_pc<=ex_pc;mem_instruction<=ex_instruction;mem_rd<=ex_rd;mem_csr_operand<=ex_csr_operand;
                    mem_second_instruction<=ex_second_instruction;mem_second_result<=ex_second_result;
                    mem_address<=(MEM_BRANCH_RESOLVE && valid_branch) ? control_target : memory_address;
                    mem_store_data<=ex_b;mem_result<=(MEM_BRANCH_RESOLVE && valid_branch) ? ex_pc+32'd4 : ex_result;
                    mem_branch_taken<=branch_taken;
                    mem_direction_index<=ex_direction_index;mem_choices<=ex_choices;
                    mem_branch_mismatch<=STATIC_CONTROL_PREDICTION ?
                        (branch_taken ? !ex_prediction_direct_match : !ex_prediction_sequential_match) : branch_taken;
                    mem_write<=ex_write;mem_is_load<=is_load;mem_is_store<=is_store;
                    local_load_waited<=PREFETCH_LOCAL_RAM && local_read_hint && local_read_hint_ready;
                    local_load_hint_first<=PREFETCH_LOCAL_RAM && local_read_hint && local_read_hint_ready;
                end
            end
            if(ex_available) begin
                ex_valid<=id_issue;multiply_started<=multiply_decode_start;
                ex_pair<=id_issue && id_pair;
                ex_branch_late1<=0;ex_branch_late2<=0;
                if(id_issue) begin
                    ex_pc<=id_pc;ex_instruction<=id_instruction;ex_immediate<=id_immediate;ex_access_error<=id_access_error;
                    ex_a<=decode_a;ex_b<=decode_b;ex_prediction<=id_prediction;
                    ex_second_instruction<=id_second_instruction;ex_second_immediate<=id_second_immediate;
                    ex_second_a<=decode_second_a;ex_second_b<=decode_second_b;ex_second_operation<=id_second_control[3:0];
                    ex_direction_index<=id_direction_index;ex_choices<=id_choices;
                    ex_branch_late1<=branch_late_admit && id_rs1!=0 && id_rs1==ex_rd;
                    ex_branch_late2<=branch_late_admit && id_rs2!=0 && id_rs2==ex_rd;
                    ex_branch_load_owner<=ex_rd;
                    {is_multiply,is_store,is_load,is_jalr,is_jal,is_branch,is_op,is_imm,is_auipc,is_lui}<=decode_instruction_class(id_instruction);
                    {alu_valid,alu_operation}<=decode_alu_control(id_instruction);
                    ex_prediction_sequential_match<=((id_pc+(id_pair ? 32'd8 : 32'd4))==id_prediction);
                    ex_prediction_direct_match<=((id_pc+id_immediate)==id_prediction);
                end
            end else begin
                // A misaligned taken-branch trap can wait for older WB to drain.
                // Retain the correct comparison operands when its load departs.
                if(mem_complete) begin
                    if(ex_branch_late1) begin ex_a<=branch_local_value;ex_branch_late1<=0;end
                    if(ex_branch_late2) begin ex_b<=branch_local_value;ex_branch_late2<=0;end
                end
                if(multiply_start) multiply_started<=1;
            end
            if(id_issue) begin
                id_valid<=skid_valid;skid_valid<=0;
                id_pair<=skid_valid && skid_pair;skid_pair<=0;
                if(skid_valid) begin
                    id_pc<=skid_pc;id_instruction<=skid_instruction;id_low_multiply<=skid_low_multiply;id_immediate<=skid_immediate;id_access_error<=skid_access_error;
                    id_prediction<=skid_prediction;id_direction_index<=skid_direction_index;id_choices<=skid_choices;
                    id_second_instruction<=skid_second_instruction;
                end
            end
            if(if_valid && !redirect && queued_after_issue<2) begin
                if_valid<=0;if_pair<=0;
                if(!id_valid || (id_issue && !skid_valid)) begin
                    id_valid<=1;id_pc<=if_pc;id_access_error<=if_access_error;
                    id_pair<=if_pair;id_second_instruction<=if_second_instruction;
                    id_instruction<=if_instruction;id_low_multiply<=decode_low_multiply(if_instruction);id_immediate<=incoming_immediate;id_prediction<=if_incoming_prediction;
                    id_direction_index<=if_direction_index;id_choices<=if_choices;
                end else begin
                    skid_valid<=1;skid_pc<=if_pc;skid_access_error<=if_access_error;
                    skid_pair<=if_pair;skid_second_instruction<=if_second_instruction;
                    skid_instruction<=if_instruction;skid_low_multiply<=decode_low_multiply(if_instruction);skid_immediate<=incoming_immediate;skid_prediction<=if_incoming_prediction;
                    skid_direction_index<=if_direction_index;skid_choices<=if_choices;
                end
            end
            if(fetch_response && !fetch_killed && !redirect) begin
                if_valid<=1;if_pc<=fetch_owner_pc;
                if_pair<=fetch_pair;if_second_instruction<=instruction_pair_data;
                if_instruction<=instruction_access_error ? 32'h13 : instruction_data;
                if_access_error<=instruction_access_error;if_prediction<=instruction_prediction;
                if_history_hit<=capture_history_hit;
                if_history_taken<=predicted_direction;
                if_direction_index<=fetch_direction_index;
                if_choices<={branch_history_hit,base_prediction,correlated_prediction};
                if_history_target<=branch_history_targets[fetch_owner_pc[7:2]];
                if_indirect_hit<=capture_indirect_hit;
                if_indirect_target<=indirect_targets[fetch_owner_pc[6:2]];
                if(empty_response_to_id) begin
                    if_valid<=0;if_pair<=0;
                    id_pair<=fetch_pair;id_second_instruction<=instruction_pair_data;
                    id_valid<=1;id_pc<=fetch_owner_pc;id_access_error<=0;
                    id_instruction<=instruction_data;id_low_multiply<=decode_low_multiply(instruction_data);id_immediate<=raw_fetch_immediate;
                    id_prediction<=incoming_prediction;
                    id_direction_index<=fetch_direction_index;
                    id_choices<={branch_history_hit,base_prediction,correlated_prediction};
                end
                fetch_pc<=incoming_prediction;
            end
            case(port_owner)
                PORT_IDLE: begin
                    load_started<=0;fetch_killed<=0;
                    // Inactive payload: transaction admission below still owns requests.
                    fetch_owner_pc<=fetch_pc;
                    if(load_demand) port_owner<=PORT_LOAD;
                    else if((queued_after_issue+{2'b0,if_valid})<3 && !redirect && !machine_pending && fetch_next_allowed) begin
                        port_owner<=PORT_FETCH;fetch_age<=0;
                    end
                end
                PORT_FETCH: begin
                    // An invalid speculative placeholder never owns a bus request.
                    // Release it so older loads can use the shared port or redirect.
                    if(!fetch_owner_allowed && !fetch_admitted) begin
                        port_owner<=PORT_IDLE;fetch_killed<=0;fetch_admitted<=0;
                    end else begin
                        if(fetch_age!=15) fetch_age<=fetch_age+1'b1;
                        if(fetch_response) begin
                            fetch_killed<=0;fetch_admitted<=0;
                            // Old response consumers sample the old PC at this edge.
                            // If admission fails, PORT_IDLE makes this payload unobservable.
                            fetch_owner_pc<=incoming_prediction;
                            // The captured response consumes one reserved slot;
                            // reserve another only when the three owned IF/ID slots have space.
                            if(!fetch_killed && !redirect && !machine_pending && !load_demand && (queued_after_issue+{2'b0,if_valid})<2) begin
                                fetch_age<=0;
                            end else port_owner<=PORT_IDLE;
                        end
                    end
                end
                PORT_LOAD: begin
                    load_started<=1;
                    if(load_complete) begin port_owner<=PORT_IDLE;load_started<=0;end
                end
                default:port_owner<=PORT_IDLE;
            endcase
            if(redirect) begin
                fetch_pc<=redirect_pc;if_valid<=0;id_valid<=0;skid_valid<=0;
                if_pair<=0;id_pair<=0;skid_pair<=0;
                if(port_owner==PORT_FETCH && !fetch_response) fetch_killed<=1;
                if(redirect_response_handoff) begin
                    // Discard the completed old response; check new-owner permission anew.
                    port_owner<=PORT_FETCH;fetch_owner_pc<=redirect_pc;fetch_age<=0;
                    fetch_killed<=0;fetch_admitted<=0;
                end
                if(mem_access_fault || mem_branch_redirect) begin
                    ex_valid<=0;mem_valid<=0;multiply_started<=0;ex_branch_late1<=0;ex_branch_late2<=0;
                    ex_pair<=0;mem_pair<=0;
                end
            end
        end
    end
    endtask
    generate if(SYNC_RESET) begin:g_sync_pipeline
        always @(posedge clk) step_pipeline();
    end else begin:g_async_pipeline
        always @(posedge clk or negedge reset_n) step_pipeline();
    end endgenerate
`ifndef SYNTHESIS
    always @(posedge clk) if(reset_n && rf_ready) begin
        if(multiply_decode_start && (!multiply_early_a_ready || !multiply_early_b_ready))
            $fatal(1,"WIDE_MUL_SECOND_SOURCE_NOT_AVAILABLE");
        if(multiply_decode_start && multiply_mem_load && branch_local_value!==load_result)
            $fatal(1,"LOCAL_MUL_PAYLOAD_MISMATCH");
        if((id_valid && id_pair && (id_pc[1:0]!=0 || id_pc[5:2]==4'hf || id_access_error || !can_pair_alu(id_instruction,id_second_instruction))) ||
           (ex_valid && ex_pair && (ex_pc[1:0]!=0 || ex_pc[5:2]==4'hf || execute_trap || !can_pair_alu(ex_instruction,ex_second_instruction))) ||
           (mem_valid && mem_pair && !can_pair_alu(mem_instruction,mem_second_instruction)) ||
           (wb_valid && wb_pair && !can_pair_alu(wb_instruction,wb_second_instruction)))
            $fatal(1,"DUAL_ALU_PAIR_IDENTITY");
        if(ALIGNED_DUAL_ALU && (register_write || register_write_second) && !rf_pair_ready)
            $fatal(1,"DUAL_ALU_RF_CONFLICT");
        if(retire_second_valid && (!retire_valid || wb_pc[1:0]!=0 || wb_pc[5:2]==4'hf)) $fatal(1,"DUAL_ALU_RETIRE_OWNER");
        if(mem_valid && mem_data_fault_pending && (data_read_request || data_write_intent || data_write_enable))
            $fatal(1,"MEM_FAULT_REQUEST_ESCAPED");
        if(mem_valid && mem_data_fault_pending && (wb_valid || !side_effects_idle) && (mem_complete || mem_access_fault || ex_complete))
            $fatal(1,"MEM_FAULT_BEFORE_DRAIN");
        if(mem_access_fault && (ex_complete || id_issue)) $fatal(1,"MEM_FAULT_YOUNGER_ADVANCED");
        if(id_valid && id_low_multiply !== decode_low_multiply(id_instruction)) $fatal(1,"LOW_MUL_ID_OWNER");
        if(skid_valid && skid_low_multiply !== decode_low_multiply(skid_instruction)) $fatal(1,"LOW_MUL_SKID_OWNER");
        if(ex_valid && {is_multiply,is_store,is_load,is_jalr,is_jal,is_branch,is_op,is_imm,is_auipc,is_lui} !== decode_instruction_class(ex_instruction))
            $fatal(1,"Registered execution class lost instruction association");
        if(ex_valid && {alu_valid,alu_operation} !== decode_alu_control(ex_instruction))
            $fatal(1,"Registered ALU control lost instruction association");
        if(ex_valid && STATIC_CONTROL_PREDICTION && !execute_trap && !is_mret && !(MEM_BRANCH_RESOLVE && is_branch) &&
            prediction_mismatch !== (actual_next_pc!=ex_prediction))
            $fatal(1,"Registered prediction match lost its execute instruction association");
        if((multiply_decode_start && !multiply_decode_payload) ||
           (multiply_execute_start && multiply_decode_payload))
            $fatal(1,"EARLY_LOW_MUL_PAYLOAD_OWNER");
        if(multiply_decode_start && ex_dependency && !multiply_ex_forwardable)
            $fatal(1,"REGISTERED_MUL_UNSUPPORTED_EX_PRODUCER");
        if(multiply_decode_start && (multiply_execute_start || !id_issue ||
            multiply_decode_a!==decode_a || multiply_decode_b!==decode_b))
            $fatal(1,"EARLY_LOW_MUL_OPERAND_OR_OWNER");
        if(multiply_started && multiply_done && !mem_available)
            $fatal(1,"Multiply completion lost behind blocked MEM stage");
        if(ex_valid && (ex_branch_late1 || ex_branch_late2)) begin
            if(!is_branch || !mem_valid || !mem_is_load || mem_rd==0 || mem_rd!=ex_branch_load_owner || mem_address[31:17]!=15'h1000)
                $fatal(1,"BRANCH_LATE_OWNER_LOST");
            if(mem_complete && branch_local_value!==load_result) $fatal(1,"BRANCH_LATE_PAYLOAD_MISMATCH");
            if(ex_complete && !mem_complete) $fatal(1,"BRANCH_LATE_EARLY_COMPLETION");
        end
        if(id_issue && ex_primary_dependency && is_load && !branch_late_admit) $fatal(1,"BRANCH_LATE_INVALID_ADMISSION");
        if(mem_branch_redirect && ex_complete) $fatal(1,"MEM_BRANCH_YOUNGER_COMPLETED");
        if(mem_valid && mem_is_branch && (mem_is_load || mem_is_store || mem_write)) $fatal(1,"MEM_BRANCH_PAYLOAD_CLASS");
        if(MEM_BRANCH_RESOLVE && branch_history_update && (!mem_valid || !mem_is_branch || !mem_complete))
            $fatal(1,"MEM_BRANCH_PREDICTOR_OWNER");
        if(id_issue && dependency_wait) $fatal(1,"Issued instruction with unavailable source");
        if(skid_valid && !id_valid) $fatal(1,"Pipeline decode queue lost ordering");
        if(fetch_response && !fetch_killed && !redirect && if_valid && queued_after_issue>=2)
            $fatal(1,"Pipeline fetch exceeded reserved queue capacity");
        if(port_owner==PORT_LOAD && !load_demand) $fatal(1,"Load port lost its memory-stage owner");
        if(instruction_request && data_read_request && (!OVERLAP_LOCAL_LOAD || data_address_is_ddr)) $fatal(1,"Pipeline overlapped shared I/D requests");
    end
`endif
endmodule
