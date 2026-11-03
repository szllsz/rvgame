`timescale 1ns / 1ps
// RGB332 DDR素材读入绘图BRAM；一次缓存最多64字节，不新增BRAM。
// 地址为程序DDR分配区内偏移。先验证所有行，禁止越界命令部分绘制。
module ddr_bitmap_blit #(parameter FRAME_WIDTH=640,FRAME_HEIGHT=480) (
    input wire clk, reset_n, enable, start,
    input wire [31:0] asset_base, asset_size, source_offset,
    input wire [15:0] stride,
    input wire [9:0] width,
    input wire [8:0] height,
    input wire signed [15:0] dest_x, dest_y,
    input wire flip_x, flip_y, transparent,
    input wire [7:0] key,
    input wire [2:0] opacity, // 0..4分别为0/25/50/75/100%，默认寄存器为4
    input wire [7:0] destination_color,
    input wire [15:0] clip_x0, clip_y0, clip_x1, clip_y1,
    output wire busy,
    output reg done, fault,
    output wire pixel_valid,
    output wire [18:0] pixel_address,
    output wire [7:0] pixel_color,
    output reg [31:0] written, skipped, clipped,
    output wire [31:0] araddr,
    output wire [3:0] arlen,
    output wire arvalid,
    input wire arready,
    input wire [31:0] rdata,
    input wire [1:0] rresp,
    input wire rlast, rvalid,
    output wire rready
);
    localparam IDLE=0, CHECK=1, PREPARE=2, ADDR=3, DATA=4, EMIT=5, FINISH=6, BLEND=7;
    reg [3:0] state;
    localparam WAIT_NEXT=8, DRAIN=9;
    localparam PF_IDLE=0, PF_ADDR=1, PF_DATA=2, PF_READY=3;
    reg [1:0] pf_state;
    reg read_bank, has_next, pf_bad;
    reg [6:0] chunk_count, pf_count, pf_begin, pf_end;
    reg [9:0] next_column;
    reg [8:0] next_row;
    reg [32:0] next_offset;
    reg [31:0] pf_address;
    reg [4:0] pf_words, pf_beat;
    reg [31:0] base, size;
    reg [32:0] row_offset, check_offset;
    reg [15:0] step;
    reg [9:0] w, column;
    reg [8:0] h, row, check_row;
    reg signed [15:0] x, y;
    reg signed [16:0] flipped_x_origin,flipped_y_origin;
    reg fx, fy, use_key;
    reg [2:0] alpha;
    reg [7:0] color_key;
    reg [15:0] cx0,cy0,cx1,cy1;
    reg [31:0] request_address;
    reg [4:0] words, beat;
    reg [6:0] byte_index, byte_end;
    reg bad;
    (* ram_style="distributed" *) reg [31:0] buffer [0:31];
    // The initial and prefetched responses share one physical RAM write port.
    wire buffer_write=(state==DATA || pf_state==PF_DATA) && rvalid;
    wire [4:0] buffer_write_address=state==DATA ? {1'b0,beat[3:0]} : {!read_bank,pf_beat[3:0]};
    always @(posedge clk) begin
        if(buffer_write) buffer[buffer_write_address]<=rdata;
    end

    wire [32:0] cursor={1'b0,base}+row_offset+column;
    wire [10:0] left={1'b0,w}-{1'b0,column};
    wire [10:0] page_words=(13'd4096-{1'b0,cursor[11:2],2'b00})>>2;
    wire [4:0] limit_words=page_words<16 ? page_words[4:0] : 5'd16;
    wire [6:0] capacity={limit_words,2'b00}-{5'd0,cursor[1:0]};
    wire [6:0] bytes_this=left<capacity ? left[6:0] : capacity;
    wire [7:0] end_this={6'd0,cursor[1:0]}+bytes_this;
    wire [5:0] words_this=(end_this+3)>>2;
    wire signed [16:0] dx=fx ? flipped_x_origin-$signed({7'd0,column}) : $signed({x[15],x})+$signed({7'd0,column});
    wire signed [16:0] dy=fy ? flipped_y_origin-$signed({8'd0,row}) : $signed({y[15],y})+$signed({8'd0,row});
    wire outside=dx<0 || dx>=FRAME_WIDTH || dy<0 || dy>=FRAME_HEIGHT ||
        dx<$signed({1'b0,cx0}) || dx>=$signed({1'b0,cx1}) ||
        dy<$signed({1'b0,cy0}) || dy>=$signed({1'b0,cy1});
    wire [32:0] pf_cursor={1'b0,base}+next_offset+next_column;
    wire [10:0] pf_left={1'b0,w}-{1'b0,next_column};
    wire [10:0] pf_page_words=(13'd4096-{1'b0,pf_cursor[11:2],2'b00})>>2;
    wire [4:0] pf_limit=pf_page_words<16 ? pf_page_words[4:0] : 5'd16;
    wire [6:0] pf_capacity={pf_limit,2'b00}-{5'd0,pf_cursor[1:0]};
    wire [6:0] pf_bytes=pf_left<pf_capacity ? pf_left[6:0] : pf_capacity;
    wire [7:0] pf_end_this={6'd0,pf_cursor[1:0]}+pf_bytes;
    wire [5:0] pf_words_this=(pf_end_this+3)>>2;
    wire first_row_end=({1'b0,column}+chunk_count=={1'b0,w});
    wire next_row_end=({1'b0,next_column}+pf_count=={1'b0,w});
    wire [31:0] selected=buffer[{read_bank,byte_index[5:2]}];
    wire [7:0] source_color=selected >> {byte_index[1:0],3'b000};
    // 在RGB332量化分量上作非预乘混合，四分之一权重，四舍五入。
    function [2:0] mix3(input [2:0] s,d,a);
        reg [5:0] total;
        begin
            case(a)
                1:total={3'd0,s}+({3'd0,d}<<1)+{3'd0,d}+6'd2;
                2:total=({3'd0,s}<<1)+({3'd0,d}<<1)+6'd2;
                3:total=({3'd0,s}<<1)+{3'd0,s}+{3'd0,d}+6'd2;
                default:total={1'b0,s,2'b00};
            endcase
            mix3=total[4:2];
        end
    endfunction
    wire [2:0] blue=mix3({1'b0,source_color[1:0]},{1'b0,destination_color[1:0]},alpha);
    assign pixel_color=state==BLEND ?
        {mix3(source_color[7:5],destination_color[7:5],alpha),
         mix3(source_color[4:2],destination_color[4:2],alpha),blue[1:0]} : source_color;
    wire keyed=use_key && source_color==color_key;
    wire blend_needed=alpha>0 && alpha<4 && !outside && !keyed;
    assign pixel_address=(FRAME_WIDTH==800 ? (({10'd0,dy[8:0]}<<9)+({10'd0,dy[8:0]}<<8)+({10'd0,dy[8:0]}<<5)) : (({10'd0,dy[8:0]}<<9)+({10'd0,dy[8:0]}<<7)))+{9'd0,dx[9:0]};
    assign pixel_valid=(state==BLEND || (state==EMIT && alpha==4)) && enable && !outside && !keyed;
    assign busy=state!=IDLE;
    assign araddr=pf_state==PF_ADDR ? pf_address : request_address;
    assign arlen=(pf_state==PF_ADDR ? pf_words[3:0] : words[3:0])-4'd1;
    // Once advertised, AR remains stable even if enable falls. Drain before done.
    assign arvalid=state==ADDR || pf_state==PF_ADDR;
    assign rready=state==DATA || pf_state==PF_DATA;
    always @(posedge clk) begin
        if(!reset_n) begin
            state<=IDLE;done<=0;fault<=0;base<=0;size<=0;row_offset<=0;check_offset<=0;
            step<=0;w<=0;h<=0;column<=0;row<=0;check_row<=0;x<=0;y<=0;
            flipped_x_origin<=0;flipped_y_origin<=0;
            fx<=0;fy<=0;use_key<=0;alpha<=4;color_key<=0;cx0<=0;cy0<=0;cx1<=0;cy1<=0;
            request_address<=0;words<=0;beat<=0;byte_index<=0;byte_end<=0;bad<=0;
            written<=0;skipped<=0;clipped<=0;
            pf_state<=PF_IDLE;read_bank<=0;has_next<=0;pf_bad<=0;
            chunk_count<=0;pf_count<=0;pf_begin<=0;pf_end<=0;
            next_column<=0;next_row<=0;next_offset<=0;
            pf_address<=0;pf_words<=0;pf_beat<=0;
        end else begin
            done<=0;
            // Read the next chunk into the inactive bank while emitting this one.
            // Every advertised request is accepted and drained even after disable.
            case(pf_state)
                PF_IDLE: if(state==EMIT && has_next && enable) begin
                    pf_address<={pf_cursor[31:2],2'b00};pf_words<=pf_words_this[4:0];
                    pf_count<=pf_bytes;pf_begin<={5'd0,pf_cursor[1:0]};pf_end<=pf_end_this[6:0];
                    pf_beat<=0;pf_bad<=0;pf_state<=PF_ADDR;
                end
                PF_ADDR: if(arready) pf_state<=PF_DATA;
                PF_DATA: if(rvalid) begin
                    if(rresp!=0 || rlast!=(pf_beat==pf_words-1)) begin fault<=1;pf_bad<=1;end
                    if(rlast) begin
                        if(pf_bad || rresp!=0 || pf_beat!=pf_words-1 || !enable) begin fault<=1;pf_bad<=1;end
                        pf_state<=PF_READY;
                    end else pf_beat<=pf_beat+1'b1;
                end
                default: begin end
            endcase
            case(state)
                IDLE: if(start) begin
                    base<=asset_base;size<=asset_size;row_offset<={1'b0,source_offset};
                    check_offset<={1'b0,source_offset};step<=stride;w<=width;h<=height;
                    column<=0;row<=0;check_row<=0;x<=dest_x;y<=dest_y;
                    // Compute the far corner once; each emitted pixel needs one add/subtract.
                    flipped_x_origin<=$signed({dest_x[15],dest_x})+$signed({7'd0,width})-17'sd1;
                    flipped_y_origin<=$signed({dest_y[15],dest_y})+$signed({8'd0,height})-17'sd1;
                    read_bank<=0;has_next<=0;pf_state<=PF_IDLE;pf_bad<=0;
                    fx<=flip_x;fy<=flip_y;use_key<=transparent;color_key<=key;alpha<=opacity;
                    cx0<=clip_x0;cy0<=clip_y0;cx1<=clip_x1;cy1<=clip_y1;
                    written<=0;skipped<=0;clipped<=0;fault<=0;bad<=0;
                    if(!enable || opacity>4 || width==0 || height==0 || stride<width || asset_base[1:0]!=0 ||
                       asset_size[1:0]!=0 || {1'b0,asset_base}+{1'b0,asset_size}>33'h040000000)
                        begin fault<=1;state<=FINISH;end
                    else state<=CHECK;
                end
                CHECK: begin
                    if(!enable) begin fault<=1;state<=FINISH;end
                    else if(check_offset+{23'd0,w}>{1'b0,size}) begin fault<=1;state<=FINISH;end
                    else if(check_row==h-1) state<=PREPARE;
                    else begin check_row<=check_row+1'b1;check_offset<=check_offset+{17'd0,step};end
                end
                PREPARE: begin
                    if(!enable) begin fault<=1;state<=FINISH;end
                    else begin
                        request_address<={cursor[31:2],2'b00};words<=words_this[4:0];
                        beat<=0;byte_index<={5'd0,cursor[1:0]};byte_end<=end_this[6:0];
                        chunk_count<=bytes_this;bad<=0;state<=ADDR;
                    end
                end
                ADDR: if(arready) state<=DATA;
                DATA: if(rvalid) begin
                    if(rresp!=0 || rlast!=(beat==words-1)) begin fault<=1;bad<=1;end
                    if(rlast) begin
                        if(bad || rresp!=0 || beat!=words-1 || !enable) begin fault<=1;state<=FINISH;end
                        else begin
                            has_next<=!(first_row_end && row==h-1);
                            next_column<=first_row_end ? 10'd0 : column+chunk_count;
                            next_row<=first_row_end ? row+1'b1 : row;
                            next_offset<=first_row_end ? row_offset+{17'd0,step} : row_offset;
                            state<=EMIT;
                        end
                    end else beat<=beat+1'b1;
                end
                EMIT,BLEND: begin
                    if(!enable) begin fault<=1;state<=DRAIN;end
                    else if(state==EMIT && blend_needed) state<=BLEND;
                    else begin
                        if(outside) clipped<=clipped+1'b1;
                        else if(keyed || alpha==0) skipped<=skipped+1'b1;
                        else written<=written+1'b1;
                        state<=EMIT;
                        byte_index<=byte_index+1'b1;
                        column<=column+1'b1;
                        if(column==w-1) begin
                            column<=0;row<=row+1'b1;row_offset<=row_offset+{17'd0,step};
                            if(row==h-1) state<=FINISH;
                            else state<=WAIT_NEXT;
                        end else if(byte_index+1'b1==byte_end) state<=WAIT_NEXT;
                    end
                end
                WAIT_NEXT: begin
                    if(!enable) begin fault<=1;state<=DRAIN;end
                    else if(pf_state==PF_READY) begin
                        if(pf_bad) state<=FINISH;
                        else begin
                            read_bank<=!read_bank;column<=next_column;row<=next_row;row_offset<=next_offset;
                            byte_index<=pf_begin;byte_end<=pf_end;chunk_count<=pf_count;
                            has_next<=!(next_row_end && next_row==h-1);
                            next_column<=next_row_end ? 10'd0 : next_column+pf_count;
                            next_row<=next_row_end ? next_row+1'b1 : next_row;
                            next_offset<=next_row_end ? next_offset+{17'd0,step} : next_offset;
                            pf_state<=PF_IDLE;state<=EMIT;
                        end
                    end
                end
                DRAIN: if(pf_state==PF_IDLE || pf_state==PF_READY) begin pf_state<=PF_IDLE;state<=FINISH;end
                FINISH: begin done<=1;state<=IDLE;end
                default: begin fault<=1;state<=FINISH;end
            endcase
        end
    end
endmodule
