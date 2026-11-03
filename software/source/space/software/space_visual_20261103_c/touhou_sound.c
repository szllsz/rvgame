#include "touhou_sound.h"
#include "touhou_game.h"
#ifdef TH_HOST
void th_sound_host_write(uint32_t index,uint32_t value);
#define WRITE(i,v) th_sound_host_write(i,v)
#else
#define WRITE(i,v) (*(volatile uint32_t *)(uintptr_t)(0x10000300u+(i)*4u)=(v))
#endif
static int due(uint32_t now,uint32_t deadline){return (int32_t)(now-deadline)>=0;}
static void pulse(ThSound *s,uint32_t scan,unsigned period,unsigned amplitude,unsigned ticks){
    WRITE(0,0xb0u|amplitude);WRITE(1,0x7f);WRITE(2,period&255u);WRITE(3,0xf8u|(period>>8));
    s->pulse_active=1;s->pulse_until=scan+ticks;
}
static void noise(ThSound *s,uint32_t scan,unsigned period,unsigned amplitude,unsigned ticks,unsigned rank){
    if(s->noise_rank>rank)return;
    WRITE(12,0x30u|amplitude);WRITE(14,period);WRITE(15,0xf8);
    s->noise_rank=rank;s->noise_until=scan+ticks;
}
void th_sound_quiet(ThSound *s){
    WRITE(0,0xb0);WRITE(12,0x30);s->pulse_active=s->noise_rank=s->sequence=0;
}
void th_sound_init(ThSound *s,uint32_t scan){
    *s=(ThSound){0};s->shot_at=s->hit_at=s->graze_at=scan;th_sound_quiet(s);
    /* input_tone_init already configures APU frame/status. Pulse2 remains
     * reserved for connection tones. Never touch global volume32/mute33. */
}
void th_sound_tick(ThSound *s,uint32_t scan,uint32_t events){
    if(s->pulse_active&&due(scan,s->pulse_until)){WRITE(0,0xb0);s->pulse_active=0;}
    if(s->noise_rank&&due(scan,s->noise_until)){WRITE(12,0x30);s->noise_rank=0;}
    if(events&(TH_EVENT_HURT|TH_EVENT_DEATH))noise(s,scan,8,6,12,2);
    else if((events&TH_EVENT_HIT)&&due(scan,s->hit_at)){noise(s,scan,3,2,2,1);s->hit_at=scan+4;}
    if(events&TH_EVENT_LOSE){s->sequence=2;s->step=0;s->next=scan;}
    else if(events&TH_EVENT_WIN){s->sequence=1;s->step=0;s->next=scan;}
    if(s->sequence){
        if(due(scan,s->next)){
            static const uint16_t notes[2][3]={{338,269,226},{338,451,678}};
            if(s->step<3){pulse(s,scan,notes[s->sequence-1][s->step++],4,6);s->next=scan+6;}
            else{s->sequence=0;}
        }
        return;
    }
    if(events&(TH_EVENT_HURT|TH_EVENT_DEATH)){pulse(s,scan,678,4,8);return;}
    if((events&TH_EVENT_SHOT)&&due(scan,s->shot_at)){pulse(s,scan,169,2,3);s->shot_at=scan+8;}
    else if((events&TH_EVENT_GRAZE)&&due(scan,s->graze_at)){pulse(s,scan,100,2,2);s->graze_at=scan+20;}
}
