#include <stdint.h>
#include <stddef.h>
#include "touhou_gfx.h"
#ifndef TH_HOST
#define DRAW(o) (*(volatile uint32_t *)(uintptr_t)(0x10000200u+(o)))
#else
uint32_t *th_host_register(unsigned offset);
#define DRAW(o) (*th_host_register(o))
#endif
static unsigned sw;
static int band_top;
static int clip_l,clip_t,clip_r=TH_WIDTH,clip_b=TH_HEIGHT;
static uint8_t alpha_shadow[TH_ROI_W*TH_ROI_H];
ThRenderStats th_render_stats;
static int max(int a,int b){return a>b?a:b;}
static int min(int a,int b){return a<b?a:b;}
static int inside_roi(int x,int y){return x>=TH_ROI_X&&x<TH_ROI_X+TH_ROI_W&&y>=TH_ROI_Y&&y<TH_ROI_Y+TH_ROI_H;}
void th_wait(void){while(DRAW(16)&3u){}}
void th_native_band(unsigned band){
    th_wait();band_top=(int)band*300;
    th_clip(0,0,TH_WIDTH,TH_HEIGHT);
    if(sw){
        /* Native DMA requires an initialized staging band. The software
         * renderer still overwrites every pixel after this protocol clear. */
        DRAW(0)=0;DRAW(4)=(300u<<16)|TH_WIDTH;DRAW(8)=0;DRAW(12)=1;
        th_wait();
    }
}
int th_native_begin(void){
    for(unsigned retry=0;retry<1024;retry++){
        DRAW(12)=7;uint32_t status=DRAW(132);
        if(status&2u)return 1;
        if((status&20u)||!(DRAW(16)&8u))return 0;
        DRAW(12)=4;
    }
    return 0;
}
int th_native_flush(unsigned band){
    th_wait();DRAW(12)=8u+band;uint32_t start=DRAW(80);
    while(band?(DRAW(132)&4u):((DRAW(132)&3u)!=3u)){
        if((DRAW(16)&8u)||(DRAW(132)&16u)||DRAW(80)-start>105000000u)return 0;
    }
    return !(DRAW(16)&8u)&&!(DRAW(132)&16u);
}
void th_gfx_mode(unsigned software){sw=!!software;th_render_stats=(ThRenderStats){0};}
void th_clip(int l,int t,int r,int b){
    th_wait();clip_l=max(0,l);clip_t=max(band_top,t);clip_r=min(TH_WIDTH,r);clip_b=min(band_top+300,b);
    DRAW(92)=(unsigned)clip_l|((unsigned)(clip_t-band_top)<<16);DRAW(96)=(unsigned)clip_r|((unsigned)(clip_b-band_top)<<16);
}
static void pixel(unsigned address,unsigned color){DRAW(72)=address;DRAW(76)=color;}
void th_fill(int x,int y,int width,int height,unsigned color){
    int l=max(x,clip_l),t=max(y,clip_t),r=min(x+width,clip_r),b=min(y+height,clip_b);
    if(l>=r||t>=b)return;
    th_wait();
    if(!sw){DRAW(0)=(unsigned)l|((unsigned)(t-band_top)<<16);DRAW(4)=(unsigned)(r-l)|((unsigned)(b-t)<<16);DRAW(8)=color;DRAW(12)=1;return;}
    for(int dy=t;dy<b;dy++){
        DRAW(72)=(unsigned)(dy-band_top)*TH_WIDTH+(unsigned)l;
        for(int dx=l;dx<r;dx++)DRAW(76)=color; /* original auto-increment pixel port */
    }
    for(int dy=max(t,TH_ROI_Y);dy<min(b,TH_ROI_Y+TH_ROI_H);dy++)
        for(int dx=max(l,TH_ROI_X);dx<min(r,TH_ROI_X+TH_ROI_W);dx++)
            alpha_shadow[(dy-TH_ROI_Y)*TH_ROI_W+dx-TH_ROI_X]=(uint8_t)color;
}
static uint8_t blend(unsigned s,unsigned d,unsigned a){
    unsigned r=(((s>>5)*a+(d>>5)*(4-a)+2)>>2)<<5;
    unsigned g=((((s>>2)&7)*a+((d>>2)&7)*(4-a)+2)>>2)<<2;
    return (uint8_t)(r|g|(((s&3)*a+(d&3)*(4-a)+2)>>2));
}
void th_bitmap(const uint8_t *source,unsigned stride,unsigned width,unsigned height,int x,int y,unsigned alpha){
    th_render_stats.attempted++;
    int l=max(x,clip_l),t=max(y,clip_t),r=min(x+(int)width,clip_r),b=min(y+(int)height,clip_b);
    if(alpha<4){l=max(l,TH_ROI_X);t=max(t,TH_ROI_Y);r=min(r,TH_ROI_X+TH_ROI_W);b=min(b,TH_ROI_Y+TH_ROI_H);}
    if(l>=r||t>=b||!alpha){th_render_stats.fully_clipped++;return;}
    th_render_stats.submitted++;if(alpha<4)th_render_stats.alpha_submitted++;
    th_wait();
    if(!sw){
        /* Crop by advancing the read-only source, keeping its original stride.
         * Source/base lifetime remains valid until draw_wait at the next op. */
        source+=(unsigned)(t-y)*stride+(unsigned)(l-x);
#ifdef TH_HOST
        extern void th_host_source(const uint8_t *source);
        th_host_source(source);
#endif
        DRAW(112)=(uint32_t)(uintptr_t)source-0x80000000u;DRAW(116)=stride;
        DRAW(4)=(unsigned)(r-l)|((unsigned)(b-t)<<16);DRAW(64)=(unsigned)l|((unsigned)(t-band_top)<<16);
        DRAW(44)=0;DRAW(36)=1;DRAW(124)=alpha;DRAW(12)=6;return;
    }
    for(int dy=t;dy<b;dy++)for(int dx=l;dx<r;dx++){
        unsigned c=source[(unsigned)(dy-y)*stride+(unsigned)(dx-x)];if(!c)continue;
        if(inside_roi(dx,dy)){
            unsigned at=(unsigned)(dy-TH_ROI_Y)*TH_ROI_W+(unsigned)(dx-TH_ROI_X);
            if(alpha<4)c=blend(c,alpha_shadow[at],alpha);
            alpha_shadow[at]=(uint8_t)c;
        }
        pixel((unsigned)(dy-band_top)*TH_WIDTH+(unsigned)dx,c);
    }
}
