/* Page-local Fusion Pixel subset. No full CJK atlas or game font changes. */
#ifndef WORK_UI_H
#define WORK_UI_H
#include "game_gfx.h"
#include "native800_frame.h"
#include "work_font800_v2.h"
#ifndef WORK_HOST
#define WORK_REG(o) (*(volatile uint32_t *)(uintptr_t)(0x10000200u+(o)))
#ifdef NATIVE800_HOST
#undef WORK_REG
#define WORK_REG(o) (*native800_host_reg(o))
#endif
#else
uint32_t *work_host_reg(unsigned offset);
void work_host_label(unsigned x,unsigned y,const char *s);
#define WORK_REG(o) (*work_host_reg(o))
#endif
static void ui_rect(unsigned x,unsigned y,unsigned w,unsigned h,unsigned color)
{
    native800_fill((int)x,(int)y,(int)w,(int)h,color);
}
/* 112 cached 16x16 glyphs, tiles 4..451; sprite tiles 0..3 stay intact. */
static uint16_t ui_cache[112];
static unsigned ui_next;
static void ui_init(void)
{
    for(unsigned i=0;i<112;i++)ui_cache[i]=65535;
    ui_next=0;
}
static void ui_text(unsigned x,unsigned y,const char *s,unsigned palette)
{
#ifdef WORK_HOST
    work_host_label(x,y,s);
#endif
    const uint8_t *p=(const uint8_t *)s;
    while(*p){
        unsigned code=*p++;
        if(code>=0xe0){code=((code&15)<<12)|((p[0]&63)<<6)|(p[1]&63);p+=2;}
        else if(code>=0xc0){code=((code&31)<<6)|(*p++&63);}
        if(code!=32){
            unsigned g=0;while(g<sizeof(work800_codes)/sizeof(work800_codes[0]) && work800_codes[g]!=code)g++;
            if(g==sizeof(work800_codes)/sizeof(work800_codes[0]))ui_rect(x,y,12,12,0xe0);
            else native800_bitmap(work800_pixels[g][palette==1?1:palette==6?2:0],20,20,24,(int)x,(int)y,0,0,1u|(227u<<8),4);
        }
        x+=code<128?8:16;
    }
}
static void __attribute__((unused)) ui_number(unsigned x,unsigned y,uint32_t v,unsigned decimals,unsigned palette)
{
    char s[24];unsigned n=0;
    do{if(decimals && n==decimals)s[n++]='.';s[n++]=(char)('0'+v%10);v/=10;}while(v || n<=decimals);
    for(unsigned i=0;i<n/2;i++){char c=s[i];s[i]=s[n-i-1];s[n-i-1]=c;}
    s[n]=0;ui_text(x,y,s,palette);
}
#endif
