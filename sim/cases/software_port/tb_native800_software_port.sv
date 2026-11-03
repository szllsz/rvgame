`timescale 1ns/1ps
module tb_native800_software_port;
`include "fixture.vh"
    reg clk=0;always #4.762 clk=~clk;
    reg reset_n=0,frame_edge=0,write_enable=0,scan_de=0;
    reg [31:0] address=0,write_data=0;wire [31:0] read_data;
    reg [10:0] scan_x=0;reg [9:0] scan_y=600;wire [23:0] rgb;
    wire frame_presented,write_ready,frame_busy,frame_fault;
    wire [31:0] underflows,copies,araddr,awaddr,wdata;wire [3:0] arlen,awlen,wstrb;
    wire arvalid,rready,awvalid,wlast,wvalid,bready;
    reg [31:0] rdata=0;reg rvalid=0,rlast=0,bvalid=0;
    reg [31:0] memory[0:262143],assets[0:5119],commands[0:8191];reg [7:0] reference[0:479999];
    integer cycle=0,write_left=0,write_word=0,bdelay=0,read_left=0,read_word=0;
    integer aw_count=0,w_count=0,ar_count=0,r_count=0,presents=0;
    integer first_cycle=0,ready_cycle=0,upper_cycle=0,upper_begin=0,lower_begin=0,upper_draw=0,lower_draw=0;
    wire awready=write_left==0 && !bvalid && bdelay==0 && (!STALLED || cycle%5!=0);
    wire wready=write_left!=0 && (!STALLED || (cycle%7!=1 && cycle%7!=2));
    wire arready=read_left==0 && !rvalid && (!STALLED || cycle%4!=0);
    reg held_aw=0,held_w=0,held_ar=0;reg [31:0] prev_aw,prev_w,prev_ar;reg prev_last;reg [3:0] prev_len;

    reg cpu_clk=0;initial begin #1.3;forever #4.762 cpu_clk=~cpu_clk;end
    reg read_request=0;wire cpu_read_ready,cpu_write_ready,quiescent;
    wire [31:0] cpu_read_data,peripheral_address,peripheral_write_data;
    wire [3:0] peripheral_mask;wire peripheral_write_enable;
    cpu_peripheral_cdc #(.SHARED_VIDEO_RESET(1),.POSTED_WRITES(1)) bridge(
        .cpu_clk(cpu_clk),.peripheral_clk(clk),.reset_n(reset_n),.peripheral_reset_n(reset_n),.cpu_hold(1'b0),
        .read_request(read_request),.write_request(write_enable),.address(address),.write_data(write_data),
        .write_mask(4'b1111),.read_ready(cpu_read_ready),.write_ready(cpu_write_ready),.read_data(cpu_read_data),
        .quiescent(quiescent),.peripheral_address(peripheral_address),.peripheral_write_data(peripheral_write_data),
        .peripheral_write_mask(peripheral_mask),.peripheral_write_enable(peripheral_write_enable),
        .peripheral_write_ready(write_ready),.peripheral_read_data(read_data));

    framebuffer_engine #(.NATIVE800(1),.DDR_FRAME(1),.DDR_BITMAP(1),.FAST_TILE_ROW(1),
        .REGISTER_DRAW_START(1),.REGISTER_TILE_START(1)) dut(
        .clk(clk),.reset_n(reset_n),.frame_edge(frame_edge),.address(peripheral_address),.write_data(peripheral_write_data),
        .write_strobe(peripheral_mask),.write_enable(peripheral_write_enable),.read_data(read_data),.scan_de(scan_de),
        .scan_x(scan_x),.scan_y(scan_y),.scan_rgb(rgb),.frame_presented(frame_presented),.write_ready(write_ready),
        .asset_base(32'h10200000),.asset_size(32'd20480),.asset_enable(1'b1),.frame_base(32'h10000000),
        .frame_enable(1'b1),.frame_busy(frame_busy),.frame_fault(frame_fault),.frame_underflows(underflows),
        .frame_copies(copies),.frame_araddr(araddr),.frame_arlen(arlen),.frame_arvalid(arvalid),.frame_arready(arready),
        .frame_rdata(rdata),.frame_rresp(2'b00),.frame_rlast(rlast),.frame_rvalid(rvalid),.frame_rready(rready),
        .frame_awaddr(awaddr),.frame_awlen(awlen),.frame_awvalid(awvalid),.frame_awready(awready),
        .frame_wdata(wdata),.frame_wstrb(wstrb),.frame_wlast(wlast),.frame_wvalid(wvalid),.frame_wready(wready),
        .frame_bresp(2'b00),.frame_bvalid(bvalid),.frame_bready(bready));
    function [23:0] expand(input [7:0] p);
        expand={p[7:5],p[7:5],p[7:6],p[4:2],p[4:2],p[4:3],p[1:0],p[1:0],p[1:0],p[1:0]};
    endfunction
    function [31:0] load_word(input integer n);
        if(n>=524288)load_word=assets[n-524288];else load_word=memory[n];
    endfunction
    always @(posedge clk)begin
        cycle<=cycle+1;
        if(cycle>5000000)$fatal(1,"B2_TIMEOUT");
        if(reset_n)begin
            if(frame_fault||underflows)$fatal(1,"B2_HARDWARE_HEALTH");
            if(dut.g_ddr_frame.u_ddr_frame.copy_busy && (dut.write_pixel||dut.cpu_pixel_pending||dut.blit_pixel_valid))
                $fatal(1,"B2_DRAW_DURING_FLUSH");
            if((dut.write_pixel||dut.cpu_pixel_pending||dut.blit_pixel_valid)&&dut.staging_address>=240000)
                $fatal(1,"B2_LOCAL_ADDRESS");
            if(held_aw&&(!awvalid||awaddr!=prev_aw))$fatal(1,"B2_AW_STABLE");
            if(held_w&&(!wvalid||wdata!=prev_w||wlast!=prev_last))$fatal(1,"B2_W_STABLE");
            if(held_ar&&(!arvalid||araddr!=prev_ar||arlen!=prev_len))$fatal(1,"B2_AR_STABLE");
            held_aw<=awvalid&&!awready;prev_aw<=awaddr;held_w<=wvalid&&!wready;prev_w<=wdata;prev_last<=wlast;
            held_ar<=arvalid&&!arready;prev_ar<=araddr;prev_len<=arlen;
            if(frame_presented)begin
                if(!dut.native_ready||w_count!=120000||!ready_cycle)$fatal(1,"B2_EARLY_PRESENT");
                presents<=presents+1;
            end
            if(awvalid&&awready)begin
                if(awlen!=15||awaddr[5:0]!=0||awaddr[11:0]>4032||awaddr[18:0]>=480000)$fatal(1,"B2_AW");
                write_word<=(awaddr-32'h10000000)>>2;write_left<=16;aw_count<=aw_count+1;
            end
            if(wvalid&&wready)begin
                if(wlast!=(write_left==1)||wstrb!=15)$fatal(1,"B2_W");
                memory[write_word]<=wdata;write_word<=write_word+1;write_left<=write_left-1;w_count<=w_count+1;
                if(wlast)bdelay<=STALLED ? 8 : 1;
            end
            if(bdelay)begin bdelay<=bdelay-1;if(bdelay==1)bvalid<=1;end
            if(bvalid&&bready)bvalid<=0;
            if(arvalid&&arready)begin
                if(({1'b0,araddr[11:0]}+(arlen+1)*4)>4096)$fatal(1,"B2_AR_PAGE");
                read_word<=(araddr-32'h10000000)>>2;read_left<=arlen+1;ar_count<=ar_count+1;
            end
            if(rvalid&&rready)begin rvalid<=0;r_count<=r_count+1;end
            if(read_left && (!rvalid||rready) && (!STALLED||cycle%6!=0))begin
                rdata<=load_word(read_word);rlast<=read_left==1;rvalid<=1;
                read_word<=read_word+1;read_left<=read_left-1;
            end
        end
    end
    task tick(input integer n);begin repeat(n)@(negedge clk);end endtask

    task wr(input [31:0] a,d);
        begin
            @(negedge cpu_clk);address=a;write_data=d;write_enable=1;
            @(posedge cpu_clk);while(!cpu_write_ready)@(posedge cpu_clk);
            @(negedge cpu_clk);write_enable=0;
        end
    endtask
    task rd(input [31:0] a,output [31:0] value);
        begin
            @(negedge cpu_clk);address=a;read_request=1;
            @(posedge cpu_clk);while(!cpu_read_ready)@(posedge cpu_clk);
            value=cpu_read_data;
            @(negedge cpu_clk);read_request=0;
        end
    endtask
    reg [31:0] status;
    task idle;
        begin
            rd(32'h10000210,status);
            while(status&3)rd(32'h10000210,status);
            if(status&8)$fatal(1,"SW_PIXEL_COMMAND_ERROR status=%h",status);
        end
    endtask

    integer i,x,y,j,band;reg [7:0] got;
    initial begin
        for(i=0;i<262144;i=i+1)memory[i]=32'ha55a39c6;
        for(i=0;i<480000;i=i+1)reference[i]=((i*13)^(i>>7)^(i>>12))&255;
        tick(8);reset_n=1;tick(12);
        rd(32'h10000264,status);if(status!==((300<<16)|800))$fatal(1,"SW_PIXEL_LOCAL_GEOMETRY");
        rd(32'h10000280,status);if(status!==((600<<16)|800))$fatal(1,"SW_PIXEL_FULL_GEOMETRY");
        wr(32'h1000020c,7);
        for(band=0;band<2;band=band+1)begin
            wr(32'h10000200,0);wr(32'h10000204,(300<<16)|800);
            wr(32'h10000208,0);wr(32'h1000020c,1);idle();
            wr(32'h10000248,0);
            for(i=0;i<240000;i=i+1)wr(32'h1000024c,reference[band*240000+i]);
            idle();rd(32'h10000248,status);
            if(status!=240000)$fatal(1,"SW_PIXEL_AUTOINCREMENT band=%d got=%d",band,status);
            $display("SW_PIXEL_BAND_WRITTEN band=%0d cycles=%0d",band,cycle);
            wr(32'h1000020c,8+band);
            rd(32'h10000210,status);
            if(status&8)$fatal(1,"SW_PIXEL_UNINITIALIZED_FLUSH_REJECT");
            if(band==0)begin
                rd(32'h10000284,status);while((status&3)!=3)rd(32'h10000284,status);
                if(copies||presents||dut.native_front)$fatal(1,"SW_PIXEL_UPPER_PUBLISH");
            end else begin
                rd(32'h10000284,status);while(!(status&8))rd(32'h10000284,status);
                ready_cycle=cycle;
                if(copies||presents||w_count!=120000)$fatal(1,"SW_PIXEL_READY_IS_NOT_PRESENT");
            end
        end
        for(j=0;j<480000;j=j+1)begin
            got=memory[131072+j/4]>>(j%4*8);
            if(got!==reference[j])$fatal(1,"SW_PIXEL_DMA i=%0d got=%h want=%h",j,got,reference[j]);
        end
        for(j=120000;j<131072;j=j+1)if(memory[131072+j]!==32'ha55a39c6)$fatal(1,"SW_PIXEL_SENTINEL");
        frame_edge=1;tick(1);frame_edge=0;tick(3);
        if(copies!=1||presents!=1||dut.native_active)$fatal(1,"SW_PIXEL_PRESENT_COUNT");
        tick(1600);
        for(y=0;y<600;y=y+1)begin
            scan_y=y;scan_x=0;scan_de=0;tick(700);
            for(x=0;x<800;x=x+1)begin
                scan_x=112+x;scan_de=1;tick(1);
                if(rgb!==expand(reference[y*800+x]))$fatal(1,"SW_PIXEL_SCAN x=%0d y=%0d",x,y);
                tick(1);
            end
            scan_de=0;tick(1);if(rgb!==0)$fatal(1,"SW_PIXEL_BLANK");
        end
        if(dut.error||frame_fault||underflows)$fatal(1,"SW_PIXEL_FINAL_HEALTH");
        $display("PASS NATIVE800_SOFTWARE_PORT posted_CDC=1 pixels=480000 full_tail=64 dma_words=%0d cycles=%0d",w_count,cycle);
        $finish;
    end
endmodule
