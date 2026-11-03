/* One immutable 800x600 image, two qualified 800x300 staging submissions.
 * Uses the already qualified native BEGIN/FLUSH/abort contract, without a
 * second framebuffer or changes to CPU clocks, ISA, cache or working RAM. */
#include "native800_frame.h"
#ifndef NATIVE800_HOST
#define DRAW(o) (*(volatile uint32_t *)(uintptr_t)(0x10000200u+(o)))
#else
uint32_t *native800_host_reg(unsigned offset);
void native800_host_source(const uint8_t *source);
#define DRAW(o) (*native800_host_reg(o))
#endif
static int offset_x,offset_y,band_top,clip_l,clip_t,clip_r=800,clip_b=600;
static unsigned active;
static unsigned layout640;
static int maximum(int a,int b){return a>b?a:b;}
static int minimum(int a,int b){return a<b?a:b;}
void native800_abort(void){DRAW(12)=10;(void)DRAW(132);active=0;}
static void fail(void){
    native800_abort();
#ifndef NATIVE800_HOST
    while(*(volatile uint32_t *)(uintptr_t)0x10000114u&1u){}
    *(volatile uint32_t *)(uintptr_t)0x10000104u=0x3ff;
    *(volatile uint32_t *)(uintptr_t)0x10000114u=1;
    for(;;){}
#else
    __builtin_trap();
#endif
}
unsigned native800_active(void){return active;}
unsigned native800_top(void){return (unsigned)band_top;}
void native800_wait(void){if(active)while(DRAW(16)&3u){}}
void native800_offset(int x,int y){offset_x=x;offset_y=y;}
void native800_layout640(unsigned enabled){layout640=!!enabled;}
int native800_layout_coordinate(int v){return !layout640?v:v>=0?v*5/4:(v*5-3)/4;}
int native800_begin(void){
    for(unsigned retry=0;retry<1024;retry++){
        DRAW(12)=7;uint32_t status=DRAW(132);
        if(status&2u){active=1;return 1;}
        if((status&20u)||!(DRAW(16)&8u))fail();
        DRAW(12)=4;
    }
    fail();return 0;
}
void native800_clip(int l,int t,int r,int b){
    l=native800_layout_coordinate(l);t=native800_layout_coordinate(t);
    r=native800_layout_coordinate(r);b=native800_layout_coordinate(b);
    native800_wait();clip_l=minimum(800,maximum(0,l+offset_x));clip_r=minimum(800,maximum(0,r+offset_x));
    clip_t=minimum(band_top+300,maximum(band_top,t+offset_y));clip_b=minimum(band_top+300,maximum(band_top,b+offset_y));
    if(clip_r<clip_l)clip_r=clip_l;
    if(clip_b<clip_t)clip_b=clip_t;
    DRAW(92)=(unsigned)clip_l|((unsigned)(clip_t-band_top)<<16);
    DRAW(96)=(unsigned)clip_r|((unsigned)(clip_b-band_top)<<16);
}
void native800_band(unsigned band){
    native800_wait();band_top=(int)band*300;
    native800_clip(-offset_x,-offset_y,800-offset_x,600-offset_y);
}
void native800_clear(unsigned color){
    native800_wait();DRAW(92)=0;DRAW(96)=800u|(300u<<16);
    DRAW(0)=0;DRAW(4)=800u|(300u<<16);DRAW(8)=color;DRAW(12)=1;
    native800_wait();
    DRAW(92)=(unsigned)clip_l|((unsigned)(clip_t-band_top)<<16);
    DRAW(96)=(unsigned)clip_r|((unsigned)(clip_b-band_top)<<16);
}
void native800_fill(int x,int y,int w,int h,unsigned color){
    int right=native800_layout_coordinate(x+w),bottom=native800_layout_coordinate(y+h);
    x=native800_layout_coordinate(x);y=native800_layout_coordinate(y);w=right-x;h=bottom-y;
    x+=offset_x;y+=offset_y;
    int l=maximum(x,clip_l),t=maximum(y,clip_t),r=minimum(x+w,clip_r),b=minimum(y+h,clip_b);
    if(l>=r||t>=b)return;
    native800_wait();DRAW(0)=(unsigned)l|((unsigned)(t-band_top)<<16);
    DRAW(4)=(unsigned)(r-l)|((unsigned)(b-t)<<16);DRAW(8)=color;DRAW(12)=1;
}
void native800_tile(int x,int y,unsigned tile,unsigned palette,unsigned fx,unsigned fy){
    x=native800_layout_coordinate(x);y=native800_layout_coordinate(y);
    x+=offset_x;y+=offset_y;
    if(x+8<=clip_l||x>=clip_r||y+8<=clip_t||y>=clip_b)return;
    native800_wait();DRAW(44)=tile|(palette<<16)|(fx<<24)|(fy<<25);
    DRAW(64)=(unsigned)(uint16_t)x|((unsigned)(uint16_t)(y-band_top)<<16);DRAW(12)=5;
}
void native800_bitmap(const uint8_t *source,unsigned stride,unsigned w,unsigned h,
                      int x,int y,unsigned fx,unsigned fy,unsigned key,unsigned alpha){
    native800_bitmap_physical(source,stride,w,h,native800_layout_coordinate(x),native800_layout_coordinate(y),fx,fy,key,alpha);
}
void native800_bitmap_physical(const uint8_t *source,unsigned stride,unsigned w,unsigned h,
                              int x,int y,unsigned fx,unsigned fy,unsigned key,unsigned alpha){
    x+=offset_x;y+=offset_y;
    int l=maximum(x,clip_l),t=maximum(y,clip_t),r=minimum(x+(int)w,clip_r),b=minimum(y+(int)h,clip_b);
    if(l>=r||t>=b||!alpha)return;
    source+=(fy?h-(unsigned)(b-y):(unsigned)(t-y))*stride+(fx?w-(unsigned)(r-x):(unsigned)(l-x));
    native800_wait();
#ifdef NATIVE800_HOST
    native800_host_source(source);
#endif
    DRAW(112)=(uint32_t)(uintptr_t)source-0x80000000u;DRAW(116)=stride;
    DRAW(4)=(unsigned)(r-l)|((unsigned)(b-t)<<16);DRAW(64)=(unsigned)l|((unsigned)(t-band_top)<<16);
    DRAW(44)=(fx<<24)|(fy<<25);DRAW(36)=key;DRAW(124)=alpha;DRAW(12)=6;
}
void native800_pixel(unsigned x,unsigned y,unsigned color){
    int dx=(int)x+offset_x,dy=(int)y+offset_y;
    if(dx<clip_l||dx>=clip_r||dy<clip_t||dy>=clip_b)return;
    DRAW(72)=(unsigned)(dy-band_top)*800u+(unsigned)dx;DRAW(76)=color;
}
void native800_pixel_linear(unsigned address,unsigned color){
    unsigned base=(unsigned)band_top*800u;
    if(address<base||address>=base+240000u)return;
    DRAW(72)=address-base;DRAW(76)=color;
}
void native800_flush(unsigned band){
    native800_wait();DRAW(12)=8u+band;uint32_t start=DRAW(80);
    while(band?(DRAW(132)&4u):((DRAW(132)&3u)!=3u)){
        if((DRAW(16)&8u)||(DRAW(132)&16u)||DRAW(80)-start>105000000u)fail();
    }
    if((DRAW(16)&8u)||(DRAW(132)&16u))fail();
    if(band)active=0;
}
