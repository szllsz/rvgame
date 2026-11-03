`timescale 1ns/1ps
module tb_native800_b2_20261031;
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
    localparam BAD_PCM=0;

    reg [31:0] audio_address=0,audio_data=0,pcm_data=0;
    reg audio_write=0,pcm_write=0,audio_feed=0;
    wire pcm_ready,audio_ready,din,bclk,lrc;
    integer sent_words=0,accepted_samples=0,serial_pairs=0;
    reg [31:0] expected_crc=32'hffffffff;
    nes_audio #(.CLOCK_HZ(105000000),.SERIAL_FRAME_MEAN(1)) audio(
        .clk(clk),.reset_n(reset_n),.cpu_hold(1'b0),.address(audio_address),.write_data(audio_data),
        .write_enable(audio_write),.write_strobe(4'hf),.host_address(32'd0),
        .pcm_host_address(8'hb4),.pcm_host_write(pcm_write),.pcm_host_data(pcm_data),
        .pcm_host_ready(pcm_ready),.pcm_host_read_data(),.read_data(),.host_read_data(),
        .write_ready(audio_ready),.audio_din(din),.audio_bclk(bclk),.audio_lrc(lrc));
    function [15:0] sample_at(input integer ordinal);
        sample_at=(ordinal*7919+17)&65535;
    endfunction
    function [31:0] sample_crc(input [31:0] previous,input [15:0] value);
        reg [31:0] c;integer bit_index;
        begin
            c=previous;
            for(bit_index=0;bit_index<16;bit_index=bit_index+1)
                c=(c[0]^value[bit_index]) ? (c>>1)^32'hedb88320 : c>>1;
            sample_crc=c;
        end
    endfunction
    always @(negedge clk)begin
        pcm_write=0;
        if(reset_n && audio_feed && pcm_ready && audio.u_pcm.count<192)begin
            pcm_data={sample_at(sent_words*2+1),sample_at(sent_words*2)};
            if(BAD_PCM && sent_words==5)pcm_data[0]=!pcm_data[0];
            sent_words=sent_words+1;pcm_write=1;
        end
    end
    always @(posedge clk)begin
        if(reset_n && audio.u_pcm.accept_sample)begin
            expected_crc=sample_crc(expected_crc,sample_at(accepted_samples));
            accepted_samples=accepted_samples+1;
            #1;
            if(audio.u_pcm.held_sample!==sample_at(accepted_samples-1))$fatal(1,"NATIVE_PCM_SAMPLE_ORDER");
        end
        if(reset_n && (audio.u_pcm.underflows || audio.serial_faults || audio.serial_quiet_faults))
            $fatal(1,"NATIVE_PCM_AUDIO_FAULT");
    end
    reg collecting=0,previous_lrc=1,serial_channel=0;
    reg [15:0] serial_word=0,left_word=0,expected_word=0;
    integer serial_bits=0;
    always @(posedge bclk)begin
        if(collecting)begin
            if(serial_bits==0)expected_word=audio.frame_sample;
            serial_word={serial_word[14:0],din};serial_bits=serial_bits+1;
            if(serial_bits==16)begin
                if(serial_word!==expected_word)$fatal(1,"NATIVE_PCM_I2S_DATA");
                if(!serial_channel)left_word=serial_word;
                else begin
                    if(serial_word!==left_word)$fatal(1,"NATIVE_PCM_I2S_STEREO");
                    serial_pairs=serial_pairs+1;
                end
            end
        end
        if(lrc!==previous_lrc)begin
            if(collecting && serial_bits!=16)$fatal(1,"NATIVE_PCM_I2S_WIDTH");
            collecting=1;serial_channel=lrc;serial_bits=0;serial_word=0;
        end
        previous_lrc=lrc;
    end
    task audio_wr(input [7:0] offset,input [31:0] value);
        begin
            @(negedge clk);audio_address=32'h10000300+offset;audio_data=value;audio_write=1;
            while(!audio_ready)@(negedge clk);
            @(negedge clk);audio_write=0;
        end
    endtask
    integer paused_count;reg pause_checked=0;
    initial begin
        wait(reset_n);tick(3);audio_wr(8'h80,4);audio_wr(8'h84,0);
        audio_wr(8'h94,1);audio_feed=1;
        wait(cycle>100000);audio_wr(8'h94,3);tick(2);paused_count=audio.u_pcm.consumed;
        tick(1000);if(audio.u_pcm.consumed!=paused_count)$fatal(1,"NATIVE_PCM_PAUSE");
        audio_wr(8'h94,1);pause_checked=1;
    end
    task check_audio;
        begin
            audio_wr(8'h94,3);tick(24);
            if(audio.u_pcm.crc_bits || audio.u_pcm.crc!==expected_crc)$fatal(1,"NATIVE_PCM_CRC");
            if(audio.u_pcm.consumed!=accepted_samples || accepted_samples<100 || serial_pairs<100 || !pause_checked)
                $fatal(1,"NATIVE_PCM_COVERAGE");
            $display("PASS NATIVE_PCM_CONCURRENT samples=%0d crc=%h serial_pairs=%0d pause_resume=1",accepted_samples,~expected_crc,serial_pairs);
        end
    endtask
    framebuffer_engine #(.NATIVE800(1),.DDR_FRAME(1),.DDR_BITMAP(1),.FAST_TILE_ROW(1),
        .REGISTER_DRAW_START(1),.REGISTER_TILE_START(1)) dut(
        .clk(clk),.reset_n(reset_n),.frame_edge(frame_edge),.address(address),.write_data(write_data),
        .write_strobe(4'b1111),.write_enable(write_enable),.read_data(read_data),.scan_de(scan_de),
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
        begin address=a;write_data=d;write_enable=1;tick(1);write_enable=0;tick(1);end
    endtask
    task idle;
        begin
            address=32'h10000210;tick(1);
            while(read_data&3)tick(1);
            if(read_data&8)$fatal(1,"B2_COMMAND_ERROR %h",read_data);
        end
    endtask
    integer i,x,y,j;reg [31:0] op,arg;reg [7:0] got;
    initial begin
        $readmemh("commands.hex",commands);$readmemh("assets.hex",assets);$readmemh("reference.hex",reference);
        for(i=0;i<262144;i=i+1)memory[i]=32'ha55a39c6;
        tick(5);reset_n=1;tick(5);
        address=32'h10000264;tick(1);if(read_data!==((300<<16)|800))$fatal(1,"B2_LOCAL_GEOMETRY");
        address=32'h10000280;tick(1);if(read_data!==((600<<16)|800))$fatal(1,"B2_FULL_GEOMETRY");
        for(i=0;i<COMMAND_PAIRS;i=i+1)begin
            op=commands[i*2];arg=commands[i*2+1];
            case(op)
                32'hffff0001:idle();
                32'hffff0002:begin
                    upper_draw=cycle-upper_begin;upper_cycle=cycle;
                    wr(32'h1000020c,8);wait(dut.native_need_band&&dut.native_copy_band);tick(1);
                    if(copies||presents||dut.native_front)$fatal(1,"B2_UPPER_PUBLISH");
                    $display("B2_UPPER draw=%0d copy=%0d",upper_draw,cycle-upper_cycle);lower_begin=cycle;
                end
                32'hffff0003:begin
                    lower_draw=cycle-lower_begin;j=cycle;wr(32'h1000020c,9);wait(dut.native_ready);tick(1);ready_cycle=cycle;
                    $display("B2_LOWER draw=%0d copy=%0d",lower_draw,cycle-j);
                    if(copies||presents)$fatal(1,"B2_READY_IS_NOT_PRESENT");
                    for(j=0;j<480000;j=j+1)begin
                        got=memory[131072+j/4]>>(j%4*8);
                        if(got!==reference[j])$fatal(1,"B2_PIXEL i=%0d got=%h want=%h",j,got,reference[j]);
                    end
                    for(j=120000;j<131072;j=j+1)if(memory[131072+j]!==32'ha55a39c6)$fatal(1,"B2_SENTINEL");
                    frame_edge=1;tick(1);frame_edge=0;tick(2);
                    if(copies!=1||presents!=1||dut.native_active)$fatal(1,"B2_FRAME_COUNT");
                end
                default:begin
                    if(!first_cycle&&op==32'h1000020c&&arg==7)begin first_cycle=cycle;upper_begin=cycle;end
                    wr(op,arg);
                end
            endcase
        end
        tick(1600);
        for(y=0;y<600;y=y+1)begin
            scan_y=y;scan_x=0;scan_de=0;tick(700);
            for(x=0;x<800;x=x+1)begin
                scan_x=112+x;scan_de=1;tick(1);
                if(rgb!==expand(reference[y*800+x]))$fatal(1,"B2_SCAN x=%0d y=%0d",x,y);
                tick(1);
            end
            scan_de=0;tick(1);if(rgb!==0)$fatal(1,"B2_BLANK");
        end
        check_audio();
        $display("PASS NATIVE800_B2 stalled=%0d ready_cycles=%0d present_cycles=%0d pixels=480000 AW=%0d W=%0d AR=%0d R=%0d",STALLED,ready_cycle-first_cycle,ready_cycle-first_cycle+1,aw_count,w_count,ar_count,r_count);
        $finish;
    end
endmodule
