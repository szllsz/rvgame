#ifndef LONGRUN_PLATFORM_INPUT_H
#define LONGRUN_PLATFORM_INPUT_H
#include <stdint.h>
#ifndef INPUT_READ
#define INPUT_READ(o) (*(volatile uint32_t *)(uintptr_t)(0x10000100u+(o)))
#endif
typedef struct {uint32_t buttons,left,right,aux;} PlatformInput;
typedef struct {uint32_t direction:8,context:24;uint32_t next;} InputRepeat;
typedef struct {uint32_t initialized:1,stable:1,pending:1;uint32_t since;} InputConnection;
_Static_assert(sizeof(InputRepeat)==8 && sizeof(InputConnection)==8,"Compact input state ABI");
extern PlatformInput platform_controller;
static inline uint32_t input_poll(PlatformInput *out)
{
    if(INPUT_READ(36)!=0x49500100u){
        out->buttons=INPUT_READ(32);out->left=out->right=out->aux=0;return 1;
    }
    for(unsigned attempt=0;attempt<3;++attempt){
        uint32_t seq=INPUT_READ(40);
        out->buttons=INPUT_READ(32);out->left=INPUT_READ(44);out->right=INPUT_READ(48);out->aux=INPUT_READ(52);
        if(seq==INPUT_READ(40))return 1;
    }
    out->buttons=out->left=out->right=out->aux=0;return 0;
}
static inline void input_movement(const PlatformInput *in,int32_t *x,int32_t *y)
{
    *x=*y=0;
    if(!(in->buttons&0x10000u))return;
    if(!(in->aux&0x80000000u)){
        *x=(((in->buttons&128u)!=0)-((in->buttons&64u)!=0))*256;
        *y=(((in->buttons&32u)!=0)-((in->buttons&16u)!=0))*256;return;
    }
    int32_t sx=(int16_t)in->left,sy=(int16_t)(in->left>>16);
    uint32_t hat=(in->aux>>16)&15u;
    int32_t dx=((hat&8u)!=0)-((hat&4u)!=0),dy=((hat&2u)!=0)-((hat&1u)!=0);
    int32_t scale=dx && dy?23170:32767;
    dx*=scale;dy*=scale;
    uint32_t a=(uint32_t)(sx*sx)+(uint32_t)(sy*sy),b=(uint32_t)(dx*dx)+(uint32_t)(dy*dy);
    if(a>1073676289u)a=1073676289u;
    if(b>1073676289u)b=1073676289u;
    if(b && a<=b){*x=dx;*y=dy;}else {*x=sx;*y=sy;}
}
static inline uint32_t input_repeat(InputRepeat *r,uint32_t buttons,uint32_t pressed,uint32_t now,uint32_t context)
{
    uint32_t direction=buttons&0xf0u;
    direction=direction&16u?16u:direction&32u?32u:direction&64u?64u:direction&128u;
    if(r->context!=context || r->direction!=direction){
        r->context=context;r->direction=direction;r->next=now+31500000u;
        return (pressed&~0xf0u)|(pressed&direction);
    }
    if(direction && (int32_t)(now-r->next)>=0){
        r->next=now+9450000u;return (pressed&~0xf0u)|direction;
    }
    return pressed&~0xf0u;
}
static inline int input_connection(InputConnection *c,uint32_t connected,uint32_t now)
{
    connected=!!connected;
    if(!c->initialized){c->initialized=1;c->stable=c->pending=connected;c->since=now;return 0;}
    if(c->pending!=connected){c->pending=connected;c->since=now;return 0;}
    if(c->stable!=connected && (uint32_t)(now-c->since)>=10500000u){c->stable=connected;return connected?1:-1;}
    return 0;
}
#endif
