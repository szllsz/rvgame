`timescale 1ns/1ps
// Experimental M-only CSR state. Not yet connected to the project pipeline.
// Caller supplies committed, serialized CSR/trap/MRET events and synchronized IRQs.
// Zmmul is not the complete M extension: misa advertises I only.
module rv32_machine_control #(parameter CSR_PROBE_ENABLE=0,parameter DUAL_RETIRE=0) (
    input wire clk,input wire reset_n,input wire retire_valid,input wire retire_second_valid,
    input wire [63:0] platform_time,
    input wire csr_access,input wire csr_commit,input wire csr_write,
    input wire [11:0] csr_address,input wire [1:0] csr_command,
    input wire [31:0] csr_operand,output reg [31:0] csr_read_data,
    output wire csr_illegal,
    input wire csr_probe_access,csr_probe_write,
    input wire [11:0] csr_probe_address,input wire [1:0] csr_probe_command,
    output wire csr_probe_illegal,
    input wire trap_enter,input wire [31:0] trap_pc,
    input wire [31:0] trap_cause,input wire [31:0] trap_value,
    input wire mret_commit,
    input wire irq_external,input wire irq_software,input wire irq_timer,
    output wire interrupt_pending,output wire [31:0] interrupt_cause,
    output wire [31:0] trap_vector,output wire [31:0] return_pc,
    output wire [31:0] exception_vector,output wire [31:0] interrupt_vector
);
    reg status_mie,status_mpie;
    reg [31:0] mie,mtvec,mscratch,mepc,mcause,mtval;
    reg [63:0] cycle_count,retired_count;
    reg [2:0] count_inhibit;
    // Keep late single/dual retirement selection out of the full64-bit carry chain.
    // Carry is from the old low word; CSR writes below still override both halves.
    wire retire_two=DUAL_RETIRE && retire_second_valid;
    wire retired_low_carry=(&retired_count[31:1]) && (retired_count[0] || retire_two);
    // The address set is shared; EX legality never selects the WB address.
    function csr_address_known;
        input [11:0] address;
        begin
            case(address)
                12'h300, 12'h301, 12'h304, 12'h305, 12'h306, 12'h320, 12'h340, 12'h341, 12'h342, 12'h343, 12'h344, 12'hc01, 12'hc81, 12'hb00, 12'hc00, 12'hb80, 12'hc80, 12'hb02, 12'hc02, 12'hb82, 12'hc82, 12'hf11, 12'hf12, 12'hf14, 12'hf13:csr_address_known=1'b1;
                default:csr_address_known=1'b0;
            endcase
        end
    endfunction
    wire csr_known=csr_address_known(csr_address);
    assign csr_probe_illegal=CSR_PROBE_ENABLE && csr_probe_access &&
        (!csr_address_known(csr_probe_address) || csr_probe_command==0 ||
         (csr_probe_write && csr_probe_address[11:10]==2'b11));
    wire [31:0] mip={20'd0,irq_external,3'd0,irq_timer,3'd0,irq_software,3'd0};
    wire [31:0] enabled_interrupts=mie & mip;
    assign interrupt_pending=status_mie && (|enabled_interrupts);
    assign interrupt_cause=enabled_interrupts[11] ? 32'h8000000b :
        enabled_interrupts[3] ? 32'h80000003 : 32'h80000007;
    assign trap_vector={mtvec[31:2],2'b00}+
        ((mtvec[0] && trap_cause[31]) ? {trap_cause[29:0],2'b00} : 32'd0);
    // Separate early vector payloads from the late trap-cause/permission selection.
    // Preserve arbitrary four-byte mtvec alignment and direct/vectored modes.
    assign exception_vector={mtvec[31:2],2'b00};
    wire [31:0] external_vector=exception_vector+32'd44;
    wire [31:0] software_vector=exception_vector+32'd12;
    wire [31:0] timer_vector=exception_vector+32'd28;
    assign interrupt_vector=!mtvec[0] ? exception_vector :
        enabled_interrupts[11] ? external_vector :
        enabled_interrupts[3] ? software_vector : timer_vector;
    assign return_pc=mepc;
    assign csr_illegal=csr_access && (!csr_known || csr_command==0 ||
        (csr_write && csr_address[11:10]==2'b11));
    wire write_commit=csr_access && csr_commit && csr_write && !csr_illegal && !trap_enter && !mret_commit;
    wire [31:0] write_value=csr_command==1 ? csr_operand :
        csr_command==2 ? (csr_read_data | csr_operand) : (csr_read_data & ~csr_operand);

    always @(*) begin
        csr_read_data=0;
        case(csr_address)
            12'h300:csr_read_data=32'h1800 | (status_mie ? 32'h8 : 0) | (status_mpie ? 32'h80 : 0);
            12'h301:csr_read_data=32'h40000100;
            12'h304:csr_read_data=mie;
            12'h305:csr_read_data=mtvec;
            12'h306:csr_read_data=0; // No lower privilege modes.
            12'h320:csr_read_data={29'd0,count_inhibit};
            12'h340:csr_read_data=mscratch;
            12'h341:csr_read_data=mepc;
            12'h342:csr_read_data=mcause;
            12'h343:csr_read_data=mtval;
            12'h344:csr_read_data=mip; // Platform interrupt inputs; writes ignored.
            12'hc01:csr_read_data=platform_time[31:0];
            12'hc81:csr_read_data=platform_time[63:32];
            12'hb00,12'hc00:csr_read_data=cycle_count[31:0];
            12'hb80,12'hc80:csr_read_data=cycle_count[63:32];
            12'hb02,12'hc02:csr_read_data=retired_count[31:0];
            12'hb82,12'hc82:csr_read_data=retired_count[63:32];
            12'hf11,12'hf12,12'hf14:csr_read_data=0;
            12'hf13:csr_read_data=1;
            default:begin csr_read_data=0;end
        endcase
    end
    always @(posedge clk) begin
        if(!reset_n) begin
            status_mie<=0;status_mpie<=0;
            mie<=0;mtvec<=0;mscratch<=0;mepc<=0;mcause<=0;mtval<=0;
            cycle_count<=0;retired_count<=0;count_inhibit<=0;
        end else begin
            if(!count_inhibit[0]) cycle_count<=cycle_count+64'd1;
            if(!count_inhibit[2] && retire_valid) begin
                retired_count[31:0]<=retired_count[31:0]+(retire_two ? 32'd2 : 32'd1);
                if(retired_low_carry) retired_count[63:32]<=retired_count[63:32]+32'd1;
            end
            if(write_commit) begin
                case(csr_address)
                    12'hb00:cycle_count<={cycle_count[63:32],write_value};
                    12'hb80:cycle_count<={write_value,cycle_count[31:0]};
                    12'hb02:retired_count<={retired_count[63:32],write_value};
                    12'hb82:retired_count<={write_value,retired_count[31:0]};
                    12'h320:count_inhibit<=write_value[2:0] & 3'b101;
                    default:begin end
                endcase
            end
            if(trap_enter) begin
                mepc<={trap_pc[31:2],2'b00};mcause<=trap_cause;mtval<=trap_value;
                status_mpie<=status_mie;status_mie<=0;
            end else if(mret_commit) begin
                status_mie<=status_mpie;status_mpie<=1;
            end else if(write_commit) begin
                case(csr_address)
                    12'h300:begin status_mie<=write_value[3];status_mpie<=write_value[7];end
                    12'h304:mie<=write_value & 32'h888;
                    12'h305:mtvec<={write_value[31:2],1'b0,(write_value[1:0]==1)};
                    12'h340:mscratch<=write_value;
                    12'h341:mepc<={write_value[31:2],2'b00};
                    12'h342:mcause<=write_value;
                    12'h343:mtval<=write_value;
                    default:begin end
                endcase
            end
        end
    end
endmodule
