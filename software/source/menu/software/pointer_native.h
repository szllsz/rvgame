#ifndef NATIVE_POINTER_UI_H
#define NATIVE_POINTER_UI_H
#include <stdint.h>
#include "pointer_cursor_assets.h"
/* ABI extension within the qualified snapshot: deadzone=63 marks a pointer;
 * the actual 10/20/30/50 preset moves into right[27:22]. Hardware is unchanged. */
typedef struct {unsigned valid,x,y,click,touch,style;} NativePointer;
static inline NativePointer pointer_read(void)
{
    NativePointer p={0,0,0,0,0,0};
#if !defined(NATIVE800_HOST) && !defined(MENU_HOST) && !defined(TATO_HOST)
    volatile uint32_t *in=(volatile uint32_t *)(uintptr_t)0x10000100u;
    for(unsigned i=0;i<3;i++){
        uint32_t seq=in[10],buttons=in[8],right=in[12],aux=in[13];
        if(seq!=in[10])continue;
        if((buttons&0x10000u) && (aux&0x80000000u) && ((aux>>22)&63u)==63u && !(right&0xc0000000u)){
            p.x=right&1023u;p.y=(right>>10)&1023u;p.click=(right>>20)&1u;p.touch=(right>>21)&1u;
            p.valid=p.x<800u && p.y<600u;
            p.style=(right>>28)&3u;
        }
        break;
    }
#endif
    return p;
}
static inline unsigned pointer_press(NativePointer p,unsigned *previous,unsigned context)
{
    unsigned state=(context<<1)|(p.valid&&p.click);
    unsigned press=(state&1u) && !(*previous&1u) && (*previous>>1)==context;
    *previous=state;return press;
}
static inline unsigned pointer_hit(NativePointer p,int x,int y,int w,int h)
{return p.valid && (int)p.x>=x && (int)p.x<x+w && (int)p.y>=y && (int)p.y<y+h;}
static inline unsigned pointer_hit640(NativePointer p,int x,int y,int w,int h)
{return pointer_hit(p,x*5/4,y*5/4,w*5/4,h*5/4);}

/* Small code-native vector cursor and a transient operation strip. No image
 * downloads, extra framebuffer, or writes after a native band has committed. */
typedef void (*PointerFill)(int,int,int,int,unsigned);
static const uint8_t pointer_letters[26][5]={
 {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
 {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
 {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
 {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
 {62,65,81,33,94},{127,9,25,41,70},{38,73,73,73,50},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
 {7,8,112,8,7},{97,81,73,69,67}};
static inline void pointer_text(PointerFill fill,int x,int y,const char *s)
{
    for(;*s;s++,x+=12){
        if(*s=='-'){fill(x,y+6,10,2,0xff);continue;}
        if(*s=='+'){fill(x,y+6,10,2,0xff);fill(x+4,y+2,2,10,0xff);continue;}
        if(*s<'A'||*s>'Z')continue;
        for(unsigned col=0;col<5;col++)for(unsigned row=0;row<7;row++)
            if(pointer_letters[*s-'A'][col]&(1u<<row))fill(x+(int)col*2,y+(int)row*2,2,2,0xff);
    }
}
static inline void pointer_draw(NativePointer p,PointerFill fill)
{
    if(!p.valid)return;
    /* Direct card clicks use the normal menu hints; no virtual button strip. */
    if(p.touch){
        if(p.y>591)p.y=591;
        fill((int)p.x-6,(int)p.y-1,13,3,0x01);fill((int)p.x-1,(int)p.y-6,3,13,0x01);
        fill((int)p.x-5,(int)p.y,11,1,0xff);fill((int)p.x,(int)p.y-5,1,11,0xff);
    }else{
        const unsigned char *sprite=pointer_cursors[p.style];
        int left=(int)p.x-pointer_hot_x[p.style],top=(int)p.y-pointer_hot_y[p.style];
        for(int row=0;row<36;row++){
            if(top+row<0 || top+row>=598)continue;
            for(int x=0;x<32;){
                unsigned color=sprite[row*32+x];int start=x++;
                while(x<32 && sprite[row*32+x]==color)x++;
                if(color!=227)fill(left+start,top+row,x-start,1,color);
            }
        }
    }
}
#endif
