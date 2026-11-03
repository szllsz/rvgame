/* One-second completed draw windows; presentation FPS stays in hardware OSD. */
#define WORK_HUD_CACHE 1
#include "work_ui.h"
#include "fps_history.h"
#ifndef CAP_NATIVE_QUALIFIED
#define CAP_NATIVE_QUALIFIED 0
#endif
static void clear_cpu(unsigned x,unsigned y,unsigned w,unsigned h){
#ifndef WORK_HOST
    unsigned bottom=y+h,top=native800_top();
    if(y<top)y=top;
    if(bottom>top+300)bottom=top+300;
    if(y>=bottom)return;
    h=bottom-y;
    if(!x && w==800){WORK_REG(72)=(y-top)*800;for(unsigned i=0;i<w*h;i++)WORK_REG(76)=9;return;}
#endif
    for(unsigned row=y;row<y+h;row++){
#ifdef WORK_HOST
        for(unsigned col=x;col<x+w;col++)work_host_pixel(row*800+col,9);
#else
        WORK_REG(72)=(row-top)*800+x;
        for(unsigned col=0;col<w;col++)WORK_REG(76)=9;
#endif
    }
}
static uint32_t sample_start,sample_count,sample_left,sample_right,mean_left,mean_right;
static unsigned sample_ready;
static uint32_t hud_key[12];
static unsigned hud_cached;
static void bench_fps_reference(void){full_fps_reference();hud_cached=0;}
static void bench_reset(uint32_t now)
{sample_start=now;sample_count=sample_left=sample_right=mean_left=mean_right=sample_ready=0;}
static void bench_sample(uint32_t now,uint32_t left,uint32_t right)
{
    if(full_fps.active){full_fps.draw_sum+=left;full_fps.draw_samples++;}
    ++sample_count;sample_left+=left;sample_right+=right;
    if(now-sample_start>=105000000u){
        mean_left=sample_left/sample_count;mean_right=sample_right/sample_count;sample_ready=1;
        sample_start=now;sample_count=sample_left=sample_right=0;
    }
}
static void bench_hud(const char *title,unsigned pair,unsigned sw,unsigned count,unsigned paused,unsigned opacity)
{
    uint32_t key[12]={pair|(sw<<1),count,paused,opacity,sample_ready,mean_left,mean_right,
        full_fps.valid,full_fps.fps100[0],full_fps.fps100[1],full_fps.active|(full_fps.mode<<1),STATE(28)&4u};
    unsigned unchanged=hud_cached;
    for(unsigned i=0;i<12;i++)if(hud_key[i]!=key[i])unchanged=0;
    /* DDR staging is persistent. Pair panels never touch the header/footer:
     * retain only their unchanged UI pixels; both scenes still render every frame.
     * Fullscreen/reference clears require a fresh HUD, never a cached scene. */
    if(pair && unchanged)return;
    for(unsigned i=0;i<12;i++)hud_key[i]=key[i];
    hud_cached=pair;
    native800_layout640(1);
    ui_rect(0,0,640,80,9);ui_rect(0,416,640,64,9);
    if(pair){
        /* Remove fullscreen residue only in the unused panel margins. */
        ui_rect(0,80,8,336,9);ui_rect(312,80,16,336,9);ui_rect(632,80,8,336,9);
        if(opacity<=4){ui_rect(0,80,640,24,9);ui_rect(0,392,640,24,9);}
    }
    ui_text(12,2,title,7);ui_text(320,2,pair?"左右对照":sw?"软件绘制":"硬件加速",7);
    if(pair){
        ui_rect(316,80,8,336,0x12);
        ui_text(12,26,"软件绘制 CPU",7);ui_text(332,26,"硬件加速",7);
        ui_text(12,48,"耗时 ms",7);ui_text(332,48,"耗时 ms",7);
        if(sample_ready){ui_number(96,48,mean_left/1050u,2,7);ui_number(416,48,mean_right/1050u,2,7);}
        else {ui_text(96,48,"待采样",7);ui_text(416,48,"待采样",7);}
    }
    for(unsigned sw_mode=0;sw_mode<2;sw_mode++){
        unsigned y=pair?64:24+sw_mode*26,x=pair?(sw_mode?12:332):12;
        ui_text(x,y,sw_mode?"软件":"硬件",7);
        ui_text(x+40,y,"FPS",7);
        if(full_fps.valid&(1u<<sw_mode))ui_number(x+72,y,full_fps.fps100[sw_mode],2,7);
        else ui_text(x+72,y,"待测",7);
        if(!pair){
            ui_text(168,y,"绘制 ms",7);
            if(full_fps.valid&(1u<<sw_mode))ui_number(244,y,full_fps.draw100[sw_mode],2,7);
            else ui_text(244,y,"待测",7);
        ui_text(352,y,sw_mode==sw?"运行中":"上次实测",7);
        }
    }
    if(!pair){
        unsigned verified=CAP_NATIVE_QUALIFIED && opacity==CAP_QUALIFIED_OPACITY &&
            (sw?(CAP_SW_VERIFIED && count==CAP_SW_VERIFIED):(count==CAP_HW_DEFAULT || count==CAP_HW_VERIFIED));
        ui_text(464,50,verified?"60FPS 已验档":count>CAP_HW_VERIFIED || (sw && !CAP_SW_VERIFIED)?"压力探索":"档位探索",7);
    }
    ui_text(12,418,pair?"每侧对象":"对象数量",7);ui_number(88,418,count,0,7);
    ui_text(144,418,paused?"已暂停":"运行中",7);
    if(pair){ui_text(224,418,"绘制加速比",7);
        if(sample_ready && mean_right)ui_number(312,418,(uint32_t)((uint64_t)mean_left*100u/mean_right),2,7);
        else ui_text(312,418,"待采样",7);}
    if(opacity<=4){ui_text(416,418,"不透明度",7);ui_number(496,418,opacity*25,0,7);ui_text(528,418,"%",7);}
    ui_text(12,440,"上下 数量  A 软硬切换  B 返回主菜单",7);
    ui_text(12,460,opacity<=4?"左右 透明度  START 暂停  SELECT 返回":"START 暂停  SELECT 返回",7);
    native800_layout640(0);
}
static void fps_history_footer(uint32_t sequence,unsigned pair,unsigned sw,unsigned count,unsigned opacity,unsigned paused){
    uint32_t words[16]={0x46505332u,sequence,full_fps.valid,full_fps.fps100[0],full_fps.fps100[1],
        full_fps.frames[0],full_fps.frames[1],full_fps.ticks[0],full_fps.ticks[1],WORK_REG(28),WORK_REG(80),
        count,opacity,paused,pair|(sw<<1),~sequence};
    game_wait();
    for(unsigned i=0;i<16;i++)for(unsigned b=0;b<4;b++)native800_pixel_linear(800u*600u-128u+i*4u+b,(words[i]>>(b*8))&255u);
    game_wait();
}
