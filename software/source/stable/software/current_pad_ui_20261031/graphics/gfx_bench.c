/* rvgame graphics capability benchmark. Same scene and output pixel port.
 * No external graphics library is linked. CPU and accelerator paths share assets.
 */
#include <stdint.h>
#include <stddef.h>
#include "game_gfx.h"
#include "font.h"
#include "gfx_motion.h"
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
#endif
#define CAP_NATIVE_QUALIFIED 1
#define CAP_HW_VERIFIED 512u
#define CAP_HW_DEFAULT 512u
#define CAP_QUALIFIED_OPACITY 5u
#define CAP_SW_VERIFIED 0u
#include "bench_ui.h"
#include "capacity_meter.h"
enum { WIDTH=800, HEIGHT=600, MAX_OBJECTS=8192, CLOCK=105000000, FOOTER=WIDTH*HEIGHT-64 };
static unsigned clip_left,clip_right=WIDTH,clip_top,clip_bottom=HEIGHT;
static void pixel(unsigned address,unsigned color){
#ifdef WORK_HOST
    work_host_pixel(address,color);
#else
    native800_pixel_linear(address,color);
#endif
}
static const uint32_t levels[]={1,16,32,64,128,256,512,1024,1536,2048,3072,4096,6144,8192};
static uint8_t image[24*24],colors[8][4];
static uint16_t base_x[MAX_OBJECTS],base_y[MAX_OBJECTS];
static uint32_t sequence;
void *memset(void *p,int value,size_t n) {uint8_t *d=p;while(n--) *d++=(uint8_t)value;return p;}
static void tile(int x,int y,unsigned id,unsigned pal,unsigned fx,unsigned fy) {
    native800_tile(x,y,id,pal,fx,fy);
}
/* Retained original ASCII helper for the frozen initialization patterns. */
static void __attribute__((unused)) text(int x,int y,const char *s) {
    while(*s) {
        char c=*s++;int glyph=c>='A'&&c<='Z'?c-'A':c>='0'&&c<='9'?c-'0'+26:c=='.'?36:-1;
        if(glyph>=0) for(unsigned qy=0;qy<2;qy++) for(unsigned qx=0;qx<2;qx++)
            tile(x+(int)qx*8,y+(int)qy*8,16+(unsigned)glyph*4+qy*2+qx,7,0,0);
        x+=12;
    }
}
static void __attribute__((unused)) number(int x,int y,uint32_t value,unsigned decimals) {
    char s[16];unsigned n=0;
    do {if(decimals && n==decimals) s[n++]='.';s[n++]=(char)('0'+value%10);value/=10;}
    while(value || n<=decimals);
    for(unsigned i=0;i<n/2;i++){char t=s[i];s[i]=s[n-i-1];s[n-i-1]=t;}
    s[n]=0;text(x,y,s);
}
static void set_page(uint32_t page,uint32_t frames) {
    while(STATE(20)&1u) {}
    STATE(0)=frames&2047u;STATE(4)=page;STATE(20)=1;
}
static void present(uint32_t page) {
    game_wait();set_page(page,sequence);
}
static void initialize(void) {
    uint8_t chr[164*16]={0},indices[32],rgb[64*3]={0};
    uint32_t random=0x12345678u;
    for(unsigned i=0;i<MAX_OBJECTS;i++) {
        random=random*1664525u+1013904223u;base_x[i]=(uint16_t)(random%656u);
        random=random*1664525u+1013904223u;base_y[i]=(uint16_t)(random%368u);
    }
    /* Sample the analytic16-unit model on20 pixels, inside symmetric24px
     * padding so the existing8px tile flips remain exact. */
    for(unsigned y=0;y<24;y++) for(unsigned x=0;x<24;x++) {
        int u=((int)x-2)*4+2,v=((int)y-2)*4+2;
        int dx=u-35,dy=v-35;if(dx<0)dx=-dx;if(dy<0)dy=-dy;
        unsigned c=x>=2 && x<22 && y>=2 && y<22 && dx+dy<=35?(unsigned)((u+2*v)/15)%3+1:0;
        image[y*24+x]=(uint8_t)c;unsigned t=(y/8)*3+x/8;
        if(c&1) chr[t*16+(y&7)]|=(uint8_t)(128u>>(x&7));
        if(c&2) chr[t*16+(y&7)+8]|=(uint8_t)(128u>>(x&7));
    }
    for(unsigned glyph=0;glyph<37;glyph++) for(unsigned y=0;y<14;y++) for(unsigned x=0;x<10;x++) {
        unsigned on=glyph<36?(font[glyph][x/2]>>(y/2))&1u:(x>=4 && x<6 && y>=12);
        if(on) chr[(16+glyph*4+(y/8)*2+x/8)*16+(y&7)]|=(uint8_t)(128u>>(x&7));
    }
    for(unsigned p=0;p<8;p++) {
        colors[p][0]=9;colors[p][1]=(uint8_t)(0x21u+p*4u);
        colors[p][2]=(uint8_t)(0xe0u|((p&7u)<<2));colors[p][3]=(uint8_t)(0xfcu|(p&3u));
    }
    colors[7][1]=255;
    for(unsigned i=0;i<32;i++) {
        unsigned c=colors[i/4][i&3];indices[i]=(uint8_t)i;
        rgb[i*3]=(uint8_t)(c&0xe0);rgb[i*3+1]=(uint8_t)((c&0x1c)<<3);rgb[i*3+2]=(uint8_t)((c&3)<<6);
    }
    game_viewport(WIDTH,HEIGHT);native800_begin();native800_band(0);
    game_upload_chr(chr,0,164);game_palette(indices,rgb);
    ui_init();
    native800_clip(0,0,WIDTH,HEIGHT);
    native800_clear(9);native800_flush(0);
    native800_band(1);native800_clear(9);native800_flush(1);
    /* Initialize validity metadata for both banks outside any measured scene. */
}
static void sprite(unsigned i,int x,int y,int software) {
    x-=2;y-=2;
    unsigned fx=(i>>3)&1u,fy=(i>>4)&1u,pal=i&7u;
    if(software) {
        for(int row=0;row<24;row++) {
            int dy=y+row;if(dy<(int)clip_top || dy>=(int)clip_bottom) continue;
            unsigned sy=fy?23u-(unsigned)row:(unsigned)row;
            for(int col=0;col<24;col++) {
                int dx=x+col;if(dx<(int)clip_left || dx>=(int)clip_right) continue;
                unsigned sx=fx?23u-(unsigned)col:(unsigned)col,c=image[sy*24+sx];
                if(c) pixel((uint32_t)dy*WIDTH+(unsigned)dx,colors[pal][c]);
            }
        }
    } else {
        for(unsigned qy=0;qy<3;qy++) for(unsigned qx=0;qx<3;qx++)
            tile(x+(int)qx*8,y+(int)qy*8,(fy?2-qy:qy)*3+(fx?2-qx:qx),pal,fx,fy);
    }
}
static void render(unsigned count,uint32_t phase,int shift,int software,int reference) {
    uint32_t dx[4],dy[4];int bounded=gfx_phase_offsets(phase,dx,dy);
    if(software) {
        clear_cpu(0,0,WIDTH,HEIGHT);
    } /* The native band was already fully cleared to9 before this render. */
    for(unsigned i=0;i<count;i++) {
        int x=bounded?gfx_position_x(base_x[i],dx[i&3u],shift):
            (int)((base_x[i]+phase*((i&3u)+1u)+(unsigned)(shift+656))%656u)-8;
        int y=bounded?gfx_position_y(base_y[i],dy[(i>>2)&3u]):
            56+(int)((base_y[i]+phase*(((i>>2)&3u)+1u))%368u);
        if(reference && i<4) {
            static const int edge_x[]={-7,635,124,245},edge_y[]={60,200,-6,475};
            x=edge_x[i];y=edge_y[i];
        }
        sprite(i,x*5/4,y*5/4,software);
    }
    game_wait();
}
static void render_panel(unsigned count,uint32_t phase,int shift,unsigned left,int software,int reference){
    unsigned physical_left=left*5/4;
    clip_left=physical_left;clip_right=physical_left+380;clip_top=100;clip_bottom=520;
    native800_clip((int)physical_left,100,(int)physical_left+380,520);
    if(software)clear_cpu(physical_left,100,380,420);
    for(unsigned i=0;i<count;i++){
        uint32_t dx=phase*((i&3)+1)+(unsigned)(shift+656);
        int x=(int)left+(int)((base_x[i]+dx)%320)-8;
        int y=80+(int)((base_y[i]+phase*(((i>>2)&3)+1))%336);
        if(reference && i<4){static const int xs[]={-7,299,124,245},ys[]={84,200,74,411};x=(int)left+xs[i];y=ys[i];}
        sprite(i,x*5/4,y*5/4,software);
    }
    game_wait();clip_left=clip_top=0;clip_right=WIDTH;clip_bottom=HEIGHT;
    native800_clip(0,0,WIDTH,HEIGHT);
}
static void footer(const uint32_t values[16]) {
    game_wait();
    for(unsigned i=0;i<16;i++) for(unsigned b=0;b<4;b++)native800_pixel_linear(FOOTER+i*4u+b,(values[i]>>(b*8))&255u);
    game_wait();
}
int main(void) {
    if(DRAW(128)!=(HEIGHT<<16|WIDTH)) {set_page(0x3ff,0);for(;;){}}
    initialize();unsigned level=6,software=0,previous=0,frozen=0,paused=0,reference=0,count=512,pair=0;
    int shift=0;uint32_t phase=0,last_begin=0,epoch=0;
    bench_reset(DRAW(80));
    for(;;) {
        unsigned buttons=STATE(32)&4095u,pressed=buttons&~previous;previous=buttons;
        if(pressed&4u) {set_page(0x37f,sequence);while(STATE(20)&1u) {}for(;;){}}
        if(pressed&(512u|1024u)) {
            software=(pressed&512u)!=0;reference=1;frozen=0;count=buttons&256u?0:levels[level];
        } else if(pressed&2u) {software=!software;reference=0;frozen=0;}
        if((STATE(28)&4u) && (pressed&1u)){pair=!pair;reference=0;frozen=0;}
        if(pressed&16u && level+1<sizeof(levels)/sizeof(levels[0])) {level++;reference=0;frozen=0;}
        if(pressed&32u && level>0) {level--;reference=0;frozen=0;}
        if(pressed&8u) paused=!paused;
        if(pressed&(1u|2u|8u|16u|32u|64u|128u|512u|1024u)) {last_begin=0;bench_reset(DRAW(80));epoch++;}
        if(frozen) continue;
        if(cap_control(pressed,phase))continue;
        uint32_t work_begin=DRAW(80);
        if(!reference) {
            count=levels[level];if(!paused) phase++;
            if(buttons&64u && shift>-320) shift-=2;
            if(buttons&128u && shift<320) shift+=2;
        } else {phase=0;shift=0;}
        if(reference){bench_fps_reference();}
        else full_fps_observe(DRAW(80),DRAW(28),pair,software,count,5,paused,shift);
        uint32_t frame_begin=DRAW(80),period=last_begin?frame_begin-last_begin:0;
        last_begin=frame_begin;uint32_t draw_cycles=0,right_cycles=0,pixel_delta=0,command_delta=0,copy_cycles=0;
        ++sequence;native800_begin();
        for(unsigned band=0;band<2;band++){
        native800_band(band);native800_clear(9);hud_cached=0;
        uint32_t begin=DRAW(80),pixels=DRAW(20),commands=DRAW(24);
        if(pair)render_panel(count,phase,shift,8,1,reference);
        else render(count,phase,shift,(int)software,(int)reference);
        draw_cycles+=DRAW(80)-begin;
        if(pair){uint32_t right_begin=DRAW(80);render_panel(count,phase,shift,328,0,reference);right_cycles+=DRAW(80)-right_begin;}
        pixel_delta+=DRAW(20)-pixels;command_delta+=DRAW(24)-commands;
        if(!reference) {
            bench_hud("TILE 图块测试",pair,software,count,paused,5);
        }
        game_wait();if(!reference && band==1){cap_config(count,software,pair,5);cap_work(DRAW(80)-work_begin-copy_cycles,draw_cycles+right_cycles,phase);}
        unsigned debug_footer=reference || (STATE(28)&4u);
        if(!reference && debug_footer)fps_history_footer(sequence,pair,software,count,5,paused);
        uint32_t values[16]={0x47584631u,sequence,count,software,phase,draw_cycles,pair?right_cycles:period,
            pixel_delta,command_delta,STATE(24),reference,buttons,(uint32_t)shift,epoch,paused|(level<<8)|(pair<<16)|(sample_ready<<17),sequence^0xffffffffu};
        if(debug_footer)footer(values);
        cap_export();
        uint32_t copied=DRAW(80);native800_flush(band);copy_cycles+=DRAW(80)-copied;
        }
        if(!reference)cap_copy_present(copy_cycles);
        if(!reference)bench_sample(DRAW(80),draw_cycles,right_cycles);
        present(reference?(software?0x373:0x374):(software?0x371:0x370));if(!reference)cap_present();
        if(reference) frozen=1;
    }
}
