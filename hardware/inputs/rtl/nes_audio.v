`timescale 1ns/1ps
// RV32I MMIO -> queued NES APU writes -> filtered mono 16-bit I2S.
// Original APU effects plus a separately clocked, buffered PCM music stream.
module nes_audio #(
    parameter CLOCK_HZ=50250000,
    parameter SERIAL_FRAME_MEAN=0,
    parameter I2S_HALF_DIV=(CLOCK_HZ+1536000)/3072000
)( 
    input wire clk, input wire reset_n, input wire cpu_hold,
    input wire [31:0] address, input wire [31:0] write_data,
    input wire write_enable, input wire [3:0] write_strobe,
    input wire [31:0] host_address,
    input wire [7:0] pcm_host_address,input wire pcm_host_write,
    input wire [31:0] pcm_host_data,output wire pcm_host_ready,
    output wire [31:0] pcm_host_read_data,
    output reg [31:0] read_data, output reg [31:0] host_read_data,
    output wire write_ready,
    output reg audio_din, output reg audio_bclk, output reg audio_lrc
);
    wire selected=address[31:8]==24'h100003;
    wire register_write=selected && address[7:2]<24 && address[1:0]==0 &&
        write_enable && write_strobe==15;
    reg [12:0] fifo[0:63];
    reg [5:0] wr_ptr,rd_ptr;
    reg [6:0] count;
    reg [31:0] writes;
    reg [3:0] volume;
    reg mute;
    reg [31:0] nes_phase;
    wire nes_ce=nes_phase>=CLOCK_HZ-1789773;
    wire pop=nes_ce && count!=0;
    wire push=register_write && write_ready;
    assign write_ready=!selected || address[7:2]>=24 || count<64;
    wire [12:0] command=fifo[rd_ptr];
    wire [15:0] apu_sample;
    wire [7:0] apu_status;
    wire dma_request;
    wire [31:0] pcm_cpu_read_data;
    wire [31:0] pcm_stream_host_read_data;
    wire signed [15:0] pcm_sample;
    reg [31:0] serial_word_count,serial_last_pair,serial_faults,serial_quiet_faults;
    reg frame_silent;
    assign pcm_host_read_data=pcm_host_address==8'hd0 ? serial_word_count :
        pcm_host_address==8'hd4 ? serial_last_pair : pcm_host_address==8'hd8 ? serial_faults :
        pcm_host_address==8'hdc ? serial_quiet_faults : pcm_stream_host_read_data;
    scarlet_pcm_stream #(.CLOCK_HZ(CLOCK_HZ)) u_pcm(
        .clk(clk),.reset_n(reset_n),.cpu_hold(cpu_hold),
        .control_write(selected && write_enable && write_strobe==15 && address[7:0]==8'h94 && write_data<=3),
        .control_data(write_data[1:0]),.host_write(pcm_host_write),.host_data(pcm_host_data),
        .cpu_address(address[7:0]),.host_address(pcm_host_address),
        .host_ready(pcm_host_ready),.cpu_read_data(pcm_cpu_read_data),.host_read_data(pcm_stream_host_read_data),.sample(pcm_sample));
    function [15:0] mix_pcm;
        input [15:0] apu;input signed [15:0] music;input [3:0] shift;input silence;
        reg signed [17:0] music_scaled,mixed;
        begin
            // Six dB music headroom leaves room for the original APU effects.
            music_scaled=$signed({{2{music[15]}},music}) >>> 1;
            music_scaled=music_scaled >>> shift;
            mixed=$signed({{2{apu[15]}},apu})+music_scaled;
            mix_pcm=silence ? 16'd0 : mixed>32767 ? 16'h7fff : mixed < -32768 ? 16'h8000 : mixed[15:0];
        end
    endfunction
    APU u_apu(.clk(clk),.ce(nes_ce),.reset(!reset_n || cpu_hold),
        .ADDR(command[12:8]),.DIN(command[7:0]),.DOUT(apu_status),
        .MW(pop),.MR(1'b0),.audio_channels(5'b11111),.Sample(apu_sample),
        .DmaReq(dma_request),.DmaAck(1'b0),.DmaAddr(),.DmaData(8'd0),
        .odd_or_even(),.IRQ());
    function [31:0] read_register(input [7:0] offset);
        begin
            case(offset)
                8'h80: read_register={28'd0,volume};
                8'h84: read_register={31'd0,mute};
                8'h88: read_register=writes;
                8'h8c: read_register={15'd0,dma_request,1'b0,count,apu_status};
                8'h90: read_register=32'h41505531;
                8'h94,8'h98,8'h9c,8'ha0,8'ha4,8'ha8,8'hac,8'hb0:read_register=pcm_cpu_read_data;
                8'hb4:read_register=serial_word_count;
                8'hb8:read_register=serial_last_pair;
                8'hbc:read_register=serial_faults;
                8'hc0:read_register=serial_quiet_faults;
                default: read_register=0;
            endcase
        end
    endfunction
    // read_register also reads state: same-address write/read must refresh the reply.
    always @(address or host_address or volume or mute or writes or count or apu_status or dma_request or pcm_cpu_read_data or serial_word_count or serial_last_pair or serial_faults or serial_quiet_faults) begin
        read_data=read_register(address[7:0]);
        host_read_data=read_register(host_address[7:0]);
    end
    always @(posedge clk) begin
        if(!reset_n || cpu_hold) begin
            wr_ptr<=0;rd_ptr<=0;count<=0;writes<=0;
            volume<=3;mute<=1;nes_phase<=0;
        end else begin
            nes_phase<=nes_ce ? nes_phase+1789773-CLOCK_HZ : nes_phase+1789773;
            case({push,pop})
                2'b10: count<=count+1'b1;
                2'b01: count<=count-1'b1;
                default: begin end
            endcase
            if(push) begin
                fifo[wr_ptr]<={address[6:2],write_data[7:0]};
                wr_ptr<=wr_ptr+1'b1;writes<=writes+1'b1;
            end
            if(pop) rd_ptr<=rd_ptr+1'b1;
            if(selected && write_enable && write_strobe==15) begin
                if(address[7:0]==8'h80) volume<=write_data>8 ? 8 : write_data[3:0];
                if(address[7:0]==8'h84) mute<=write_data[0];
            end
        end
    end
    // Integer divider near 48 kHz; the original 50.25 MHz setting remains divide-by-16.
    // Average one complete I2S frame; remove DAC DC with a 1/128 IIR.
    localparam DIV_BITS=$clog2(I2S_HALF_DIV)>0 ? $clog2(I2S_HALF_DIV) : 1;
    localparam SAMPLE_CLOCKS=64*I2S_HALF_DIV;
    localparam SUM_BITS=16+$clog2(SAMPLE_CLOCKS);
    reg [DIV_BITS-1:0] divider;
    reg [4:0] bit_position;
    reg [SUM_BITS-1:0] sample_sum;
    reg signed [24:0] dc;
    // Complete before the first MSB of the new I2S frame is transmitted.
    // The frame sum is < SAMPLE_CLOCKS*65536, so sixteen low-bit steps suffice.
    localparam REM_BITS=$clog2(SAMPLE_CLOCKS);
    reg [REM_BITS-1:0] serial_remainder;
    reg [15:0] serial_dividend,serial_quotient,serial_mean_result;
    reg [4:0] serial_steps;
    reg serial_active,serial_done,serial_hold,serial_silence;
    reg [3:0] serial_shift;
    reg signed [24:0] serial_dc;
    wire frame_boundary=divider==I2S_HALF_DIV-1 && audio_bclk && bit_position==31;
    wire [REM_BITS:0] serial_trial={serial_remainder,serial_dividend[15]};
    wire serial_quotient_bit=serial_trial>=SAMPLE_CLOCKS;
    wire [REM_BITS:0] serial_next_remainder=serial_quotient_bit ? serial_trial-SAMPLE_CLOCKS : serial_trial;
    wire signed [24:0] serial_difference=$signed({1'b0,serial_mean_result,8'd0})-serial_dc;
    wire signed [24:0] serial_scaled=(serial_difference>>>8) >>> serial_shift;
    wire [15:0] serial_next_sample=serial_silence ? 16'd0 :
        serial_scaled>32767 ? 16'h7fff : serial_scaled < -32768 ? 16'h8000 : serial_scaled[15:0];
    always @(posedge clk) begin
        if(!reset_n) begin
            serial_remainder<=0;serial_dividend<=0;serial_quotient<=0;serial_mean_result<=0;
            serial_steps<=0;serial_active<=0;serial_done<=0;
            serial_hold<=0;serial_silence<=1;serial_shift<=8;serial_dc<=0;
        end else begin
            serial_done<=0;
            if(SERIAL_FRAME_MEAN && frame_boundary) begin
                serial_remainder<=sample_sum>>16;serial_dividend<=sample_sum[15:0];
                serial_quotient<=0;serial_steps<=16;serial_active<=1;
                serial_hold<=cpu_hold;serial_silence<=cpu_hold || mute || volume==0;
                serial_shift<=8-volume;serial_dc<=dc;
            end else if(serial_active) begin
                serial_remainder<=serial_next_remainder;
                serial_dividend<={serial_dividend[14:0],1'b0};
                serial_quotient<={serial_quotient[14:0],serial_quotient_bit};
                serial_steps<=serial_steps-1'b1;
                if(serial_steps==1) begin
                    serial_active<=0;serial_done<=1;
                    serial_mean_result<={serial_quotient[14:0],serial_quotient_bit};
                end
            end
        end
    end
`ifndef SYNTHESIS
    initial if(SERIAL_FRAME_MEAN && 2*I2S_HALF_DIV<=17)
        $fatal(1,"I2S bit interval is too short for exact serial frame averaging");
    always @(posedge clk) if(reset_n && SERIAL_FRAME_MEAN && frame_boundary && serial_active)
        $fatal(1,"Audio frame averaging did not finish before the next frame");
`endif
    wire [SUM_BITS-1:0] sample_sum_next=sample_sum+apu_sample;
    wire signed [24:0] mean_q8;
    generate if (SERIAL_FRAME_MEAN) begin : g_serial_mean
        assign mean_q8=$signed({1'b0,serial_mean_result,8'd0});
    end else if (SAMPLE_CLOCKS==1024) begin : g_original_mean
        assign mean_q8=$signed({1'b0,sample_sum[25:10],8'd0});
    end else begin : g_clock_mean
        reg [15:0] mean_pipeline;
        // At the sample boundary this equals the complete preceding box average.
        // The look-ahead sum compensates for this register's one-clock latency.
        always @(posedge clk) begin
            if (!reset_n) mean_pipeline<=0;
            else mean_pipeline<=sample_sum_next/SAMPLE_CLOCKS;
        end
        assign mean_q8=$signed({1'b0,mean_pipeline,8'd0});
    end endgenerate
    wire signed [24:0] difference=mean_q8-dc;
    wire signed [24:0] scaled=(difference>>>8) >>> (8-volume);
    wire [15:0] next_sample=cpu_hold || mute || volume==0 ? 16'd0 :
        scaled>32767 ? 16'h7fff : scaled < -32768 ? 16'h8000 : scaled[15:0];
    reg [15:0] frame_sample;
    reg signed [15:0] pcm_frame_sample;
    always @(posedge clk) begin
        if(!reset_n) begin
            divider<=0;bit_position<=31;sample_sum<=0;dc<=0;
            frame_sample<=0;pcm_frame_sample<=0;frame_silent<=1;audio_din<=0;audio_bclk<=0;audio_lrc<=1;
        end else begin
            divider<=divider==I2S_HALF_DIV-1 ? 0 : divider+1'b1;
            sample_sum<=sample_sum_next;
            if(divider==I2S_HALF_DIV-1) begin
                audio_bclk<=!audio_bclk;
                if(audio_bclk) begin
                    bit_position<=bit_position+1'b1;
                    if(bit_position==31) begin
                        audio_lrc<=0;audio_din<=frame_sample[0];
                        pcm_frame_sample<=pcm_sample;
                        if(!SERIAL_FRAME_MEAN) begin
                            frame_sample<=mix_pcm(next_sample,pcm_sample,8-volume,cpu_hold || mute || volume==0);dc<=cpu_hold ? 0 : dc+(difference>>>7);
                            frame_silent<=cpu_hold || mute || volume==0;
                        end
                        sample_sum<=apu_sample;
                    end else if(bit_position==15) begin
                        audio_lrc<=1;audio_din<=frame_sample[0];
                    end else if(bit_position<15) audio_din<=frame_sample[15-bit_position];
                    else audio_din<=frame_sample[31-bit_position];
                end
            end
            if(SERIAL_FRAME_MEAN && serial_done) begin
                frame_sample<=mix_pcm(serial_next_sample,pcm_frame_sample,serial_shift,serial_silence);
                frame_silent<=serial_silence;
                dc<=serial_hold ? 0 : serial_dc+(serial_difference>>>7);
            end
        end
    end
    // Passive decoder samples the actual serialized DIN/LRC at each BCLK rise.
    // This is an on-board digital witness, not a physical DAC/listening claim.
    reg rx_previous_lrc,rx_collecting,rx_channel,rx_left_valid,rx_silent;
    reg [4:0] rx_bits;
    reg [15:0] rx_shift,rx_expected,rx_left;
    wire [15:0] rx_word={rx_shift[14:0],audio_din};
    always @(posedge clk) begin
        if(!reset_n || cpu_hold) begin
            serial_word_count<=0;serial_last_pair<=0;serial_faults<=0;serial_quiet_faults<=0;
            rx_previous_lrc<=!reset_n ? 1'b1 : audio_lrc;rx_collecting<=0;rx_channel<=0;rx_left_valid<=0;rx_silent<=1;
            rx_bits<=0;rx_shift<=0;rx_expected<=0;rx_left<=0;
        end else if(divider==I2S_HALF_DIV-1 && !audio_bclk) begin
            if(rx_collecting) begin
                if(rx_bits==0)begin rx_expected<=frame_sample;if(!rx_channel)rx_silent<=frame_silent;end
                rx_shift<=rx_word;rx_bits<=rx_bits+1'b1;
                if(rx_bits==15)begin
                    if(rx_word!=rx_expected)serial_faults<=serial_faults+1'b1;
                    if(!rx_channel)begin rx_left<=rx_word;rx_left_valid<=1;end
                    else if(rx_left_valid)begin
                        serial_word_count<=serial_word_count+1'b1;serial_last_pair<={rx_word,rx_left};
                        if(rx_word!=rx_left)serial_faults<=serial_faults+1'b1;
                        if(rx_silent && (rx_word!=0 || rx_left!=0))serial_quiet_faults<=serial_quiet_faults+1'b1;
                    end
                end
            end
            if(audio_lrc!=rx_previous_lrc)begin
                if(rx_collecting && rx_bits!=15)serial_faults<=serial_faults+1'b1;
                rx_collecting<=1;rx_channel<=audio_lrc;rx_bits<=0;rx_shift<=0;
            end
            rx_previous_lrc<=audio_lrc;
        end
    end
endmodule
