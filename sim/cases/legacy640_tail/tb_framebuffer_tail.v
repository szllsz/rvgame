`timescale 1ns/1ps
// Public MMIO -> real staging RAM -> real AXI DMA -> independent pixel oracle.
module tb_framebuffer_tail;
  reg clk=0;always #5 clk=!clk;
  reg reset_n=0,frame_edge=0,write_enable=0,scan_de=0;
  reg [31:0] address=0,write_data=0;
  reg [10:0] scan_x=0;reg [9:0] scan_y=0;
  wire [31:0] read_data,araddr,awaddr,wdata,underflows,copies;
  wire [3:0] arlen,awlen,wstrb;
  wire arvalid,rready,awvalid,wlast,wvalid,bready,frame_busy,frame_fault,frame_presented,write_ready;
  wire [23:0] scan_rgb;
  reg [31:0] rdata=0;reg rvalid=0,rlast=0,bvalid=0;
  integer ticks=0,write_left=0,write_word=0,bd=0,read_left=0,read_byte=0,rd=0;
  integer copied_words=0,bursts=0,i,j,x,y,lane,swaps=0,scan_pixels=0;
  reg [7:0] expected[0:307199];reg [31:0] memory[0:262143];
  wire awready=write_left==0 && bd==0 && !bvalid && ticks%5!=2;
  wire wready=write_left!=0 && ticks%3!=1;
  wire arready=read_left==0 && !rvalid && ticks%7!=3;
  framebuffer_engine #(.DDR_FRAME(1),.DDR_BITMAP(1),.FAST_TILE_ROW(1),.COMPACT_FRAME(1),
    .REGISTER_DRAW_START(1),.REGISTER_TILE_START(1)) dut(
    .clk(clk),.reset_n(reset_n),.frame_edge(frame_edge),.address(address),.write_data(write_data),
    .write_strobe(4'b1111),.write_enable(write_enable),.read_data(read_data),
    .scan_de(scan_de),.scan_x(scan_x),.scan_y(scan_y),.scan_rgb(scan_rgb),
    .frame_presented(frame_presented),.write_ready(write_ready),
    .asset_base(32'h20000000),.asset_size(32'h100000),.asset_enable(1'b1),
    .frame_base(32'h10000000),.frame_enable(1'b1),.frame_busy(frame_busy),.frame_fault(frame_fault),
    .frame_underflows(underflows),.frame_copies(copies),.frame_araddr(araddr),.frame_arlen(arlen),
    .frame_arvalid(arvalid),.frame_arready(arready),.frame_rdata(rdata),.frame_rresp(2'b00),
    .frame_rlast(rlast),.frame_rvalid(rvalid),.frame_rready(rready),.frame_awaddr(awaddr),
    .frame_awlen(awlen),.frame_awvalid(awvalid),.frame_awready(awready),.frame_wdata(wdata),
    .frame_wstrb(wstrb),.frame_wlast(wlast),.frame_wvalid(wvalid),.frame_wready(wready),
    .frame_bresp(2'b00),.frame_bvalid(bvalid),.frame_bready(bready));
  function [7:0] pattern(input integer n);
    pattern=(n*37)^(n>>8)^(n>>16);
  endfunction
  function [7:0] asset(input integer n);
    asset=(n*19)^(n>>3);
  endfunction
  function [23:0] rgb(input [7:0] p);
    rgb={p[7:5],p[7:5],p[7:6],p[4:2],p[4:2],p[4:3],p[1:0],p[1:0],p[1:0],p[1:0]};
  endfunction
  function [7:0] blend(input [7:0] s,d);
    integer r,g,b;
    begin r=((s>>5)+(d>>5)+1)/2;g=(((s>>2)&7)+((d>>2)&7)+1)/2;
      b=((s&3)+(d&3)+1)/2;blend=(r<<5)|(g<<2)|b;end
  endfunction
  always @(posedge clk) begin
    ticks<=ticks+1;
    if(ticks>4000000) $fatal(1,"FRAME_TAIL_TIMEOUT");
    if(reset_n && (dut.write_pixel || dut.cpu_pixel_pending || dut.blit_pixel_valid) &&
       dut.staging_address>=307200) $fatal(1,"FRAME_TAIL_WRITE_RANGE");
    if(frame_presented) begin
      if(!dut.dma_finished || copied_words!=(swaps+1)*76800) $fatal(1,"FRAME_TAIL_EARLY_PRESENT");
      swaps<=swaps+1;
    end
    if(awvalid && awready) begin
      if(awlen!=15 || awaddr[5:0]!=0 || awaddr<32'h10000000 || awaddr>=32'h10100000 ||
         awaddr[18:0]>=307200) $fatal(1,"FRAME_TAIL_AW_RANGE");
      write_word<=(awaddr-32'h10000000)>>2;write_left<=16;bursts<=bursts+1;
    end
    if(wvalid && wready) begin
      if(wlast!=(write_left==1) || wstrb!=15) $fatal(1,"FRAME_TAIL_W_PROTOCOL");
      for(lane=0;lane<4;lane=lane+1)
        if(wdata[lane*8 +: 8]!==expected[(write_word%131072)*4+lane])
          $fatal(1,"FRAME_TAIL_PIXEL addr=%d lane=%d got=%h expected=%h",(write_word%131072)*4,lane,wdata[lane*8 +: 8],expected[(write_word%131072)*4+lane]);
      memory[write_word]<=wdata;write_word<=write_word+1;write_left<=write_left-1;
      copied_words<=copied_words+1;if(write_left==1) bd<=7;
    end
    if(bd!=0) begin bd<=bd-1;if(bd==1) bvalid<=1;end
    if(bvalid && bready) bvalid<=0;
    if(arvalid && arready) begin
      if(araddr[1:0]!=0 || {1'b0,araddr[11:0]}+(arlen+1)*4>4096) $fatal(1,"FRAME_TAIL_AR_RANGE");
      read_byte<=araddr;read_left<=arlen+1;rd<=5;
    end
    if(rd!=0) rd<=rd-1;
    if(!rvalid && read_left!=0 && rd==0 && ticks%4!=1) begin
      if(read_byte>=32'h20000000) rdata<={asset(read_byte-32'h20000000+3),asset(read_byte-32'h20000000+2),asset(read_byte-32'h20000000+1),asset(read_byte-32'h20000000)};
      else rdata<=memory[(read_byte-32'h10000000)>>2];
      rlast<=read_left==1;rvalid<=1;
    end
    if(rvalid && rready) begin rvalid<=0;read_byte<=read_byte+4;read_left<=read_left-1;end
  end
  task wr(input [31:0] a,d);
    begin @(negedge clk);address=a;write_data=d;write_enable=1;
      @(negedge clk);write_enable=0;end
  endtask
  task idle;
    begin wait(!dut.busy && !dut.launch_pending && !dut.cpu_pixel_pending);@(negedge clk);
      if(dut.error || frame_fault) $fatal(1,"FRAME_TAIL_DRAW_ERROR");end
  endtask
  task clear_frame;
    begin wr('h10000200,0);wr('h10000204,(480<<16)|640);wr('h10000208,'h24);
      wr('h1000020c,1);idle();for(i=0;i<307200;i=i+1) expected[i]='h24;end
  endtask
  task present;
    integer target;
    begin target=swaps+1;wr('h1000020c,2);wait(dut.dma_finished);
      repeat(9) @(negedge clk);if(swaps!=target-1) $fatal(1,"FRAME_TAIL_FRAME_EDGE");
      frame_edge=1;@(negedge clk);frame_edge=0;wait(swaps==target);end
  endtask
  initial begin
    repeat(5) @(negedge clk);reset_n=1;clear_frame();
    wr('h10000248,0);
    for(i=0;i<307200;i=i+1) begin wr('h1000024c,pattern(i));expected[i]=pattern(i);end
    idle();wr('h10000248,307200);wr('h1000024c,'hff);
    if(!dut.error) $fatal(1,"FRAME_TAIL_MISSING_BOUNDS_REJECT");
    wr('h1000020c,4);idle();present();
    // Reinitialize drawing bank, exercise alpha + color key across the last RAM word.
    clear_frame();wr('h10000204,(1<<16)|640);wr('h10000240,479<<16);
    wr('h10000270,0);wr('h10000274,640);wr('h1000027c,2);wr('h10000224,('h42<<8)|1);
    wr('h1000020c,6);idle();
    for(i=0;i<640;i=i+1) if(asset(i)!='h42) expected[479*640+i]=blend(asset(i),'h24);
    present();repeat(400) @(negedge clk);
    for(y=0;y<600;y=y+1) begin
      for(x=0;x<1024;x=x+1) begin
        @(negedge clk);scan_x=x;scan_y=y;scan_de=1;
        @(posedge clk);#1;
        if(x>=112 && x<912) begin
          if(scan_rgb!==rgb(expected[(y*4/5)*640+(x-112)*4/5])) $fatal(1,"FRAME_TAIL_SCAN_PIXEL x=%d y=%d",x,y);
          scan_pixels=scan_pixels+1;
        end else if(scan_rgb!==24'h000000) $fatal(1,"FRAME_TAIL_BORDER");
      end
      @(negedge clk);scan_de=0;repeat(32) @(negedge clk);
    end
    if(underflows!=0 || frame_fault || copied_words!=153600 || bursts!=9600 || copies!=2 || scan_pixels!=480000)
      $fatal(1,"FRAME_TAIL_FINAL_COUNTS");
    $display("PASS: FRAME_TAIL public_pixels=307200 dma_words=153600 bursts=9600 scan_pixels=307200 alpha_key_tail=640");$finish;
  end
endmodule
