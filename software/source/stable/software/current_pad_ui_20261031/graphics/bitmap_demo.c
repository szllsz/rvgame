/* DDR RGB332 bitmap capability demo. Original generated test assets. */
#include <stdint.h>
#include <stddef.h>
#include "game_gfx.h"
#include "native800_demo_assets.h"
#include "font.h"
#define DRAW(o) (*(volatile uint32_t *)(uintptr_t)(0x10000200u+(o)))
#define STATE(o) (*(volatile uint32_t *)(uintptr_t)(0x10000100u+(o)))
#ifdef NATIVE800_HOST
#undef DRAW
#undef STATE
#define DRAW(o) (*native800_host_reg(o))
#define STATE(o) (*native800_host_state(o))
#endif
#ifdef WORK_HOST
#undef DRAW
#define DRAW(o) (*work_host_reg(o))
#undef STATE
#define STATE(o) (*work_host_state(o))
uint32_t *work_host_reg(unsigned offset);
uint32_t *work_host_state(unsigned offset);
void work_host_pixel(unsigned address,unsigned color);
void work_host_blit(const uint8_t *,unsigned,unsigned,unsigned,int,int,unsigned,unsigned,unsigned,unsigned);
#endif
#define CAP_NATIVE_QUALIFIED 1
#define CAP_HW_VERIFIED 96u
#define CAP_HW_DEFAULT 96u
#define CAP_QUALIFIED_OPACITY 5u
#define CAP_SW_VERIFIED 0u
#include "bench_ui.h"
#include "capacity_meter.h"
#define fill ui_rect
enum { WIDTH=800,HEIGHT=600,CLOCK=105000000,FOOTER=WIDTH*HEIGHT-64 };
static const unsigned levels[]={1,2,4,8,16,32,64,96,128,192,256,384,512};
static uint32_t sequence;
static unsigned workset;
static unsigned clip_left,clip_right=WIDTH,clip_top,clip_bottom=HEIGHT;
static void pixel(unsigned address,unsigned color){
#ifdef WORK_HOST
    work_host_pixel(address,color);
#else
    native800_pixel_linear(address,color);
#endif
}
void *memset(void *p,int v,size_t n){uint8_t *d=p;while(n--) *d++=(uint8_t)v;return p;}
static void page(unsigned p){while(STATE(20)&1u){}STATE(0)=sequence&2047u;STATE(4)=p;STATE(20)=1;}
static void present(unsigned p){game_wait();page(p);}
static void bitmap(const uint8_t *source,unsigned stride,unsigned w,unsigned h,int x,int y,unsigned fx,unsigned fy,unsigned key,int sw){
    game_wait();
    if(sw){
        for(unsigned sy=0;sy<h;sy++) for(unsigned sx=0;sx<w;sx++){
            int dx=x+(int)(fx?w-1-sx:sx),dy=y+(int)(fy?h-1-sy:sy);
            uint8_t c=source[sy*stride+sx];
            if(dx>=(int)clip_left && dx<(int)clip_right && dy>=(int)clip_top && dy<(int)clip_bottom && (!key || c)){
                pixel((unsigned)dy*WIDTH+(unsigned)dx,c);
            }
        }
    }else{
#ifdef WORK_HOST
        work_host_blit(source,stride,w,h,x,y,fx,fy,key,4);
#else
        native800_bitmap(source,stride,w,h,x,y,fx,fy,key,4);
#endif
    }
}
static void render(unsigned count,unsigned phase,int sw,int reference,unsigned packed){
    clip_left=0;clip_right=WIDTH;clip_top=reference?0:54*5/4;clip_bottom=reference?HEIGHT:438*5/4;
    native800_clip(0,(int)clip_top,WIDTH,(int)clip_bottom);
    if(sw)clear_cpu(0,clip_top,WIDTH,clip_bottom-clip_top);
    if(count)bitmap(packed?backdrop_packed:backdrop+1,packed?400:403,400,225,200,175,0,0,0,sw);
    for(unsigned i=0;i<count;i++){
        int x=(int)((i*79u+phase*(i%3u+1u))%704u)-32;
        int y=64+(int)((i*53u+phase)%384u);
        if(reference && i<4){static const int xs[]={-15,610,180,340},ys[]={100,250,-12,455};x=xs[i];y=ys[i];}
        bitmap(packed?(workset?workset_hero[i&15u]:hero_packed):hero+1,packed?80:83,80,80,x*5/4,y*5/4,i&1u,(i>>1)&1u,1,sw);
    }
    game_wait();native800_clip(0,0,WIDTH,HEIGHT);
}
static void render_panel(unsigned count,unsigned phase,unsigned left,int sw,int reference,unsigned packed){
    clip_left=left*5/4;clip_right=clip_left+380;clip_top=100;clip_bottom=520;
    native800_clip((int)clip_left,100,(int)clip_right,520);
    if(sw)clear_cpu(clip_left,100,380,420);
    if(count)bitmap(packed?backdrop_packed:backdrop+1,packed?400:403,400,225,(int)clip_left,175,0,0,0,sw);
    for(unsigned i=0;i<count;i++){
        int x=(int)left+(int)((i*79u+phase*(i%3u+1u))%368u)-32;
        int y=80+(int)((i*53u+phase)%336u)-32;
        if(reference && i<4){static const int xs[]={-15,290,180,140},ys[]={100,250,68,405};x=(int)left+xs[i];y=ys[i];}
        bitmap(packed?(workset?workset_hero[i&15u]:hero_packed):hero+1,packed?80:83,80,80,x*5/4,y*5/4,i&1u,(i>>1)&1u,1,sw);
    }
    game_wait();clip_left=clip_top=0;clip_right=WIDTH;clip_bottom=HEIGHT;native800_clip(0,0,WIDTH,HEIGHT);
}
static void footer(uint32_t data[16]){game_wait();for(unsigned i=0;i<16;i++)for(unsigned b=0;b<4;b++)native800_pixel_linear(FOOTER+i*4u+b,(data[i]>>(8*b))&255);game_wait();}
static void initialize(void){
    uint8_t chr[152*16]={0},indices[32],rgb[192]={0};
    for(unsigned g=0;g<37;g++)for(unsigned y=0;y<14;y++)for(unsigned x=0;x<10;x++){
        unsigned on=g<36?(font[g][x/2]>>(y/2))&1u:(x>=4 && x<6 && y>=12);
        if(on)chr[(4+g*4+(y/8)*2+x/8)*16+(y&7)]|=(uint8_t)(128u>>(x&7));
    }
    for(unsigned i=0;i<32;i++)indices[i]=(uint8_t)i;
    rgb[29*3]=rgb[29*3+1]=rgb[29*3+2]=255;
    game_viewport(WIDTH,HEIGHT);native800_begin();native800_band(0);
    game_upload_chr(chr,0,152);game_palette(indices,rgb);
    ui_init();
    native800_clip(0,0,WIDTH,HEIGHT);
    native800_clear(9);native800_flush(0);
    native800_band(1);native800_clear(9);native800_flush(1);
}
int main(void){
    if(DRAW(128)!=(HEIGHT<<16|WIDTH) || !(DRAW(120)&4u)){page(0x3ff);for(;;){}}
    initialize();unsigned level=7,sw=0,previous=0,reference=0,frozen=0,paused=0,count=96,phase=0,epoch=0,packed=1,pair=0;
    uint32_t last_begin=0;bench_reset(DRAW(80));
    for(;;){
        unsigned buttons=STATE(32)&4095u,pressed=buttons&~previous;previous=buttons;
        if(pressed&4u){page(0x38f);while(STATE(20)&1u){}for(;;){}}
        if(pressed&(512u|1024u)){sw=(pressed&512u)!=0;reference=1;frozen=0;count=buttons&256u?0:levels[level];packed=(buttons&2048u)!=0;}
        else if(pressed&2u){sw=!sw;reference=0;frozen=0;}
        if((STATE(28)&4u) && (pressed&1u)){pair=!pair;reference=0;frozen=0;}
        if(pressed&16u && level+1<sizeof(levels)/sizeof(levels[0])){level++;reference=0;frozen=0;}
        if(pressed&32u && level>0){level--;reference=0;frozen=0;}
        if(pressed&8u)paused=!paused;
        if(pressed&64u){workset=!workset;reference=0;frozen=0;}
        if(pressed&(1u|2u|8u|16u|32u|64u|512u|1024u)){epoch++;last_begin=0;bench_reset(DRAW(80));}
        if(frozen)continue;
        if(cap_control(pressed,phase))continue;
        uint32_t work_begin=DRAW(80);
        if(!reference){packed=1;count=levels[level];if(!paused)phase++;}else phase=0;
        if(reference){bench_fps_reference();}
        else full_fps_observe(DRAW(80),DRAW(28),pair,sw,count,5+workset,paused,0);
        uint32_t frame_begin=DRAW(80),period=last_begin?frame_begin-last_begin:0;last_begin=frame_begin;
        uint32_t cycles=0,right_cycles=0,delta_pixels=0,delta_commands=0,copy_cycles=0;
        ++sequence;native800_begin();for(unsigned band=0;band<2;band++){
        native800_band(band);native800_clear(9);hud_cached=0;
        uint32_t begin=DRAW(80),pixels=DRAW(20),commands=DRAW(24);
        if(pair)render_panel(count,phase,8,1,reference,packed);
        else render(count,phase,(int)sw,(int)reference,packed);
        cycles+=DRAW(80)-begin;delta_pixels+=DRAW(20)-pixels;delta_commands+=DRAW(24)-commands;
        if(pair){uint32_t right_begin=DRAW(80);render_panel(count,phase,328,0,reference,packed);right_cycles+=DRAW(80)-right_begin;}
        if(DRAW(16)&8u){native800_abort();page(0x3ff);for(;;){}}
        if(!reference){
            bench_hud("DDR 位图测试",pair,sw,count,paused,5+workset);
            ui_text(576,538,"素材集",7);ui_number(640,538,workset?16:1,0,7);
        }
        game_wait();if(!reference && band==1){cap_words[39]=workset;cap_config(count,sw,pair,5);cap_work(DRAW(80)-work_begin-copy_cycles,cycles+right_cycles,phase);}
        unsigned debug_footer=reference || (STATE(28)&4u);
        if(!reference && debug_footer)fps_history_footer(sequence,pair,sw,count,5,paused);
        uint32_t data[16]={0x424d5031u,sequence,count,sw,phase,cycles,pair?right_cycles:period,delta_pixels,delta_commands,STATE(24),reference,buttons,packed,epoch,paused|(level<<8)|(pair<<16)|(sample_ready<<17),sequence^0xffffffffu};
        if(debug_footer)footer(data);
        cap_export();uint32_t copied=DRAW(80);native800_flush(band);copy_cycles+=DRAW(80)-copied;
        }
        if(!reference)cap_copy_present(copy_cycles);
        if(!reference)bench_sample(DRAW(80),cycles,right_cycles);
        present(reference?(sw?0x383:0x384):(sw?0x381:0x380));if(!reference)cap_present();if(reference)frozen=1;
    }
}
