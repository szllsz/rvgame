/* Reuse rvgame's original 5x7 letter/number patterns for the benchmark page. */
#include "coremark.h"
#include "work_ui.h"
#define REG(a) (*(volatile ee_u32 *)(uintptr_t)(a))
#ifdef WORK_HOST
#undef REG
#define REG(a) (*work_host_reg((a)-0x10000200u))
#endif
static const unsigned char font[36][5] __attribute__((unused))={
 {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
 {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
 {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
 {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
 {62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
 {7,8,112,8,7},{97,81,73,69,67},
 {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},
 {24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
 {54,73,73,73,54},{6,73,73,41,30}
};
static void wait_draw(void) {
#ifdef WORK_HOST
    game_wait();
#else
    native800_wait();
#endif
}
static void rect(unsigned x,unsigned y,unsigned w,unsigned h,unsigned color) {
    native800_fill((int)x,(int)y,(int)w,(int)h,color);
}
static const char *diagnostic_log;
static unsigned diagnostic_size;
void coremark_log(const char *text,unsigned size){diagnostic_log=text;diagnostic_size=size;}
static void render_log(void){
    if(!diagnostic_log)return;
    native800_layout640(0);native800_offset(0,0);native800_clip(0,0,800,600);
    native800_fill(0,592,800,8,0);
    native800_wait();
    for(unsigned i=0;i<=diagnostic_size;i++)native800_pixel(i%800u,592u+i/800u,i<diagnostic_size?(unsigned char)diagnostic_log[i]:0);
    native800_layout640(1);native800_offset(0,0);
}
static void latin_text(unsigned x,unsigned y,const char *s,unsigned color,unsigned scale) {
    while(*s) {
        char c=*s++;int glyph=c>='A'&&c<='Z'?c-'A':c>='0'&&c<='9'?c-'0'+26:-1;
        if(glyph>=0 && scale>=2 && scale<=4){
            unsigned w=(5*scale*5+3)/4,h=(7*scale*5+3)/4;
            native800_bitmap(work800_latin[scale-2][glyph],w,w,h,(int)x,(int)y,0,0,1u|(227u<<8),4);
        }
        if(c=='.') rect(x+2*scale,y+6*scale,scale,scale,color);
        if(c=='/') for(unsigned k=0;k<5;k++) rect(x+(4-k)*scale,y+(k+1)*scale,scale,scale,color);
        x+=6*scale;
    }
}
static void text(unsigned x,unsigned y,const char *s,unsigned color,unsigned scale){
    (void)scale;ui_text(x,y,s,color==0xe0?6:7);
}
static void number(unsigned x,unsigned y,ee_u32 value,unsigned decimals,unsigned scale) {
    char buf[20];unsigned n=0;
    do {if(decimals && n==decimals) buf[n++]='.';buf[n++]=(char)('0'+value%10);value/=10;}
    while(value || n<=decimals);
    for(unsigned i=0;i<n/2;i++){char c=buf[i];buf[i]=buf[n-i-1];buf[n-i-1]=c;}
    buf[n]=0;latin_text(x,y,buf,0xff,scale);
}
void coremark_page(ee_u32 ticks,int valid) {
    ui_init();game_viewport(640,480);
    uint8_t indices[32],rgb[192]={0};for(unsigned i=0;i<32;i++)indices[i]=(uint8_t)i;
    rgb[75]=255;rgb[87]=rgb[88]=rgb[89]=255;
    native800_begin();native800_band(0);game_palette(indices,rgb);
    for(unsigned band=0;band<2;band++){
    native800_band(band);native800_clear(0x09);
    rect(0,0,640,480,0x09);rect(20,20,600,50,0x12);
    text(36,36,"RISC-V CPU 标准测评",0xff,2);
    text(36,92,"COREMARK",0xfc,3);
    if(!ticks) {text(36,132,"测试运行中",0xff,2);number(220,132,ITERATIONS,0,2);text(300,132,"次迭代，请稍候",0xff,2);}
    else {
        ee_u32 score100=(ee_u32)(((uint64_t)ITERATIONS*platform_clock_hz*100u+ticks/2)/ticks);
        ee_u32 mhz1000=(ee_u32)(((uint64_t)ITERATIONS*1000000u*1000u+ticks/2)/ticks);
        number(36,132,score100,2,4);
        text(250,140,"COREMARK/MHZ",0xfc,2);number(420,140,mhz1000,3,2);
        text(36,206,valid?"CRC 校验通过  2K 单核":"CRC 校验失败  结果无效",valid?0x3c:0xe0,2);
        text(36,242,"时间 s",0xbf,2);number(108,242,(ee_u32)((uint64_t)ticks*1000u/platform_clock_hz),3,2);
        text(264,242,"周期数",0xbf,2);number(360,242,ticks,0,2);
    }
#if VALIDATION_RUN
    text(36,180,"正确性验证",0xbf,2);
#else
    text(36,180,"性能测试",0xbf,2);
#endif
    text(36,282,"频率",0xff,2);
    number(108,282,platform_clock_hz/1000u,3,2);
    text(204,282,"MHZ RV32I ZMMUL",0xff,2);
    text(36,312,"GCC 13.3  O3 LTO  DDR 程序",0xff,2);
    text(36,342,"工作 RAM 128K  Cache 64K  指令缓冲 4K",0xff,2);
    text(36,376,"保守核心 LUT 6696",0xbf,2);
    text(252,376,"CM/LUT",0xfc,2);
    if(ticks) {
        uint64_t denominator=(uint64_t)ticks*6696u;
        ee_u32 efficiency100000=(ee_u32)(((uint64_t)ITERATIONS*platform_clock_hz*100000u+denominator/2u)/denominator);
        number(348,376,efficiency100000,5,2);
    }
    text(36,406,"整机 LUT 20372  BRAM 137/140  DSP 4",0xbf,2);
    text(36,444,"A 重新测试  B 返回  SELECT 主菜单",0xfc,2);
    render_log();native800_flush(band);}
}
static void hex16(unsigned x,unsigned y,ee_u16 value) {
    char word[5];for(unsigned i=0;i<4;i++)word[i]="0123456789ABCDEF"[(value>>(12-i*4))&15];word[4]=0;
    latin_text(x,y,word,0xff,2);
}
void coremark_suite_page(const ee_u32 *ticks,const int *valid,const ee_u16 crc[2][3],unsigned phase,unsigned details) {
    ui_init();game_viewport(640,480);
    uint8_t indices[32],rgb[192]={0};for(unsigned i=0;i<32;i++)indices[i]=(uint8_t)i;
    rgb[75]=255;rgb[87]=rgb[88]=rgb[89]=255;
    native800_begin();native800_band(0);game_palette(indices,rgb);
    for(unsigned band=0;band<2;band++){
    native800_band(band);native800_clear(0x09);
    rect(0,0,640,480,0x09);rect(20,20,600,50,0x12);
    text(36,36,details?"RISC-V CPU 测评详情":"RISC-V CPU 测评",0xff,2);
    if(phase<2) {
        text(36,92,phase==0?"正在测试性能  1/2":"正在验证计算结果  2/2",0xff,2);
        text(36,126,"每轮 4000 次迭代，请稍候",0xbf,2);
    } else text(36,92,"测评完成",0xff,2);
    rect(28,162,584,110,0x0a);
    text(44,176,"运行性能",0xbf,2);
    if(ticks[0] && valid[0]) {
        ee_u32 score=(ee_u32)(((uint64_t)ITERATIONS*platform_clock_hz*100u+ticks[0]/2)/ticks[0]);
        number(44,210,score,2,4);text(244,222,"CoreMark",0xff,2);
        text(356,176,"每 MHz 性能",0xbf,2);
        number(356,214,(ee_u32)(((uint64_t)ITERATIONS*1000000000u+ticks[0]/2)/ticks[0]),3,3);
    } else text(44,218,phase==0?"测试中":"结果无效",phase==0?0xbf:0xe0,2);
    text(36,296,"工作频率",0xbf,2);number(212,296,platform_clock_hz/1000u,3,2);text(328,296,"MHz",0xff,2);
    text(36,334,"性能测试用时",0xbf,2);
    if(ticks[0]){number(212,334,(ee_u32)((uint64_t)ticks[0]*1000u/platform_clock_hz),3,2);text(328,334,"秒",0xff,2);}
    else text(212,334,"等待结果",0xbf,2);
    text(36,372,"计算正确性",0xbf,2);
    text(212,372,phase<2?(phase==0?"等待验证":"验证中"):(valid[0]&&valid[1]?"验证通过":"验证失败"),phase<2?0xbf:(valid[0]&&valid[1]?0x3c:0xe0),2);
    if(details) {
        rect(28,86,584,330,0x09);
        text(36,92,"两轮均检查 CRC，计时须达到 10 秒",0xff,2);
        text(36,136,"性能配置",0xbf,2);text(220,136,valid[0]?"CRC 与计时通过":"未通过",valid[0]?0x3c:0xe0,2);
        text(36,174,"验证配置",0xbf,2);text(220,174,valid[1]?"CRC 与计时通过":"未通过",valid[1]?0x3c:0xe0,2);
        text(180,210,"LIST",0xbf,2);text(300,210,"MATRIX",0xbf,2);text(420,210,"STATE",0xbf,2);
        text(36,242,"性能 CRC",0xbf,2);text(36,274,"验证 CRC",0xbf,2);
        for(unsigned run=0;run<2;run++)for(unsigned part=0;part<3;part++)hex16(180+part*120,242+run*32,crc[run][part]);
        text(36,314,"验证用时",0xbf,2);number(220,314,(ee_u32)((uint64_t)ticks[1]*1000u/platform_clock_hz),3,2);text(332,314,"秒",0xff,2);
        text(36,350,"GCC 13.3  O3 LTO  RV32I ZMMUL",0xbf,2);
        text(36,386,"数据 2K  单核  每轮 4000 次迭代",0xbf,2);
    }
    rect(24,424,592,1,0x12);
    text(36,444,phase<2?"测评进行中  B/SELECT 返回主菜单":"A 重测  B 返回  Y 详情  SELECT 主菜单",0xff,2);
    render_log();native800_flush(band);}
}
void coremark_present(void) { wait_draw(); }
