#ifndef LONGRUN_INPUT_TONE_H
#define LONGRUN_INPUT_TONE_H
#include <stdint.h>
#ifndef INPUT_APU_WRITE
#define INPUT_APU_WRITE(i,v) (*(volatile uint32_t *)(uintptr_t)(0x10000300u+(i)*4u)=(v))
#endif
typedef struct {uint32_t kind,step,next;} InputTone;
static inline void input_tone_init(void)
{
    INPUT_APU_WRITE(23,0xc0);INPUT_APU_WRITE(21,15);INPUT_APU_WRITE(4,0xb0);
}
static inline void input_tone_tick(InputTone *tone,int event,uint32_t now)
{
    if(event){tone->kind=event>0?1u:2u;tone->step=0;tone->next=now;}
    if(!tone->kind || (int32_t)(now-tone->next)<0)return;
    if(tone->step==2){INPUT_APU_WRITE(4,0xb0);tone->kind=0;return;}
    unsigned period=(tone->kind==1)==(tone->step==0)?338u:169u;
    INPUT_APU_WRITE(4,0xb6);INPUT_APU_WRITE(5,0x7f);
    INPUT_APU_WRITE(6,period&255u);INPUT_APU_WRITE(7,0xf8u|(period>>8));
    ++tone->step;tone->next=now+7000000u;
}
#endif
