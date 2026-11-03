`timescale 1ns / 1ps
// 单在途AXI3读事务；显示请求优先，AR背压期间固定所有者与地址。
module ddr_read_mux (
    input wire clk, reset_n,
    input wire scan_reserve, // Keep a scan line refill contiguous across AR preparation gaps.
    input wire [31:0] scan_araddr, blit_araddr,
    input wire [3:0] scan_arlen, blit_arlen,
    input wire scan_arvalid, blit_arvalid,
    output wire scan_arready, blit_arready,
    output wire scan_rvalid, blit_rvalid,
    input wire scan_rready, blit_rready,
    output wire [31:0] araddr,
    output wire [3:0] arlen,
    output wire arvalid,
    input wire arready,
    input wire rvalid, rlast,
    output wire rready,
    output wire busy
);
    reg [1:0] state;
    reg owner;
    reg [31:0] held_address;
    reg [3:0] held_length;
    assign araddr=held_address;assign arlen=held_length;assign arvalid=state==1;
    assign scan_arready=state==1 && !owner && arready;
    assign blit_arready=state==1 && owner && arready;
    assign scan_rvalid=state==2 && !owner && rvalid;
    assign blit_rvalid=state==2 && owner && rvalid;
    assign rready=state==2 && (owner ? blit_rready : scan_rready);
    assign busy=state!=0;
    always @(posedge clk) begin
        if(!reset_n) begin state<=0;owner<=0;held_address<=0;held_length<=0;end
        else case(state)
            0: if(scan_arvalid || (blit_arvalid && !scan_reserve)) begin
                owner<=!scan_arvalid;
                held_address<=scan_arvalid ? scan_araddr : blit_araddr;
                held_length<=scan_arvalid ? scan_arlen : blit_arlen;
                state<=1;
            end
            1: if(arready) state<=2;
            2: if(rvalid && rready && rlast) state<=0;
            default: state<=0;
        endcase
    end
endmodule
