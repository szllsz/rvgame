/* Original Power Punch streams through the shared PCM mixer. Existing event
 * effects remain APU approximations, independent of the fixed-rate music. */
#include "arena_audio.h"
#include "native_boot.h"
#ifdef TATO_HOST
void arena_audio_host_write(uint32_t index,uint32_t value);
#define WRITE(i,v) arena_audio_host_write(i,v)
#else
#define WRITE(i,v) (*(volatile uint32_t *)(uintptr_t)(0x10000300u+(i)*4u)=(v))
#endif
static uint32_t volume=3,muted,effect_ticks,effect_kind,last_effect_period,music_control,accent_ticks;
static uint32_t shot_kind,shot_cooldown,pickup_cooldown;
static void pulse(uint32_t base,uint32_t period,uint32_t amplitude)
{
    WRITE(base,0xb0u|amplitude);WRITE(base+1,0x7f);
    WRITE(base+2,period&255u);WRITE(base+3,0xf8u|(period>>8));
}
void arena_audio_init(void)
{
    uint32_t boot=native_boot_audio();volume=boot&15u;muted=(boot>>8)&1u;effect_ticks=effect_kind=last_effect_period=accent_ticks=0;
    WRITE(32,volume);WRITE(33,muted);WRITE(23,0xc0);WRITE(21,15);
    WRITE(0,0xb0);WRITE(4,0xb0);WRITE(8,0);WRITE(12,0x30);
    WRITE(37,1);music_control=1;
    shot_kind=shot_cooldown=pickup_cooldown=0;
}
void arena_music_pause(void) {if(music_control!=3){WRITE(37,3);music_control=3;}}
uint32_t arena_audio_volume(void) {return volume;}
uint32_t arena_audio_muted(void) {return muted;}
void arena_audio_connection(int event)
{
    if(event){effect_kind=event>0?9:10;effect_ticks=8;last_effect_period=0;}
}
void arena_audio_controls(uint32_t mode,uint32_t pressed)
{
    if(mode!=TATO_PAUSE && mode!=TATO_TITLE) return;
    if(mode==TATO_PAUSE && (pressed&128u) && volume<8) ++volume;
    if(mode==TATO_PAUSE && (pressed&64u) && volume) --volume;
    if(pressed&512u) muted^=1;
    WRITE(32,volume);
}
void arena_audio_tick(const TatoGame *g)
{
    uint32_t paused=g->mode==TATO_PAUSE || g->mode==TATO_EXIT;
    uint32_t command=paused?3u:1u;
    if(command!=music_control){WRITE(37,command);music_control=command;}
    WRITE(33,muted||paused);
    if(paused) {WRITE(0,0xb0);WRITE(12,0x30);accent_ticks=0;return;}
    if(shot_cooldown)--shot_cooldown;
    if(pickup_cooldown)--pickup_cooldown;
    uint32_t events=g->audio_events;
    if(effect_ticks && effect_kind>=9 && effect_kind<=10) { /* Stable USB edge has one short cue. */ }
    else if(events&SOUND_DEATH) {effect_kind=1;effect_ticks=30;}
    else if(events&SOUND_HURT) {effect_kind=1;effect_ticks=12;}
    /* Source-bound UI approximations: button_focus.wav rises from ~275 Hz;
     * button_press.wav is a short broad click. 60 Hz APU envelopes, not PCM. */
    else if(events&SOUND_UI_PRESS) {effect_kind=8;effect_ticks=4;last_effect_period=0;}
    else if(events&SOUND_UI_FOCUS) {effect_kind=7;effect_ticks=5;last_effect_period=0;}
    else if(events&(SOUND_BUY|SOUND_LEVEL|SOUND_WAVE)) {effect_kind=2;effect_ticks=18;}
    else if(events&SOUND_FAIL) {effect_kind=3;effect_ticks=8;}
    else if((events&SOUND_MATERIAL_PICKUP) && !pickup_cooldown && effect_ticks<4) {effect_kind=11;effect_ticks=3;pickup_cooldown=4;}
    else if((events&SOUND_MATERIAL_ATTRACT) && !pickup_cooldown && !effect_ticks) {effect_kind=12;effect_ticks=2;pickup_cooldown=8;}
    else if(events&(SOUND_HEALING|SOUND_GARDEN) && effect_ticks<6) {effect_kind=4;effect_ticks=6;}
    else if(events&SOUND_LASER && effect_ticks<4) {effect_kind=5;effect_ticks=4;}
    else if(events&SOUND_ROCKET && effect_ticks<4) {effect_kind=6;effect_ticks=4;}
    else if(events&SOUND_KILL && !effect_ticks) {effect_kind=2;effect_ticks=3;}
    if(effect_ticks) {
        static const uint16_t focus_periods[5]={399,330,236,154,135};
        static const uint8_t focus_amplitudes[5]={9,8,4,1,0};
        static const uint16_t press_periods[4]={209,108,155,209};
        static const uint8_t press_amplitudes[4]={9,2,1,0};
        uint32_t period=effect_kind==1?200+(30-effect_ticks)*16:
            effect_kind==2?(effect_ticks>12?338:effect_ticks>6?253:169):
            effect_kind==4?210+effect_ticks*22:effect_kind==5?110+effect_ticks*38:effect_kind==6?500+effect_ticks*65:600;
        if(effect_kind==11 || effect_kind==12){
            unsigned index=(effect_kind==11?3u:2u)-effect_ticks;
            static const uint16_t pickup_periods[3]={110,76,56};
            static const uint8_t pickup_amplitudes[3]={3,2,0};
            period=effect_kind==11?pickup_periods[index]:(index?170u:240u);
            pulse(4,period,effect_kind==11?pickup_amplitudes[index]:(index?0u:1u));
        }else if(effect_kind>=9){
            unsigned step=(8u-effect_ticks)/4u;
            period=(effect_kind==9)==(step==0)?338u:169u;
            if(period!=last_effect_period)pulse(4,period,6);
        }else if(effect_kind>=7){
            unsigned index=(effect_kind==7?5u:4u)-effect_ticks;
            period=effect_kind==7?focus_periods[index]:press_periods[index];
            pulse(4,period,effect_kind==7?focus_amplitudes[index]:press_amplitudes[index]);
        }else if(period!=last_effect_period) pulse(4,period,effect_kind>=4?3:6);
        last_effect_period=period;--effect_ticks;
    } else if(last_effect_period) {WRITE(4,0xb0);last_effect_period=0;}
    if((events&(SOUND_ATTACK|SOUND_HIT|SOUND_EXPLOSION)) && !shot_cooldown) {
        shot_kind=events&SOUND_EXPLOSION?3u:events&SOUND_LASER?5u:events&SOUND_ROCKET?6u:
            events&SOUND_TURRET?2u:events&SOUND_FLAME?4u:events&SOUND_HIT?1u:0u;
        accent_ticks=shot_kind==3?5u:3u;shot_cooldown=shot_kind==3?9u:shot_kind==2?6u:5u;
    }
    if(accent_ticks){
        /* Short decays and a quiet interval prevent frequent attacks from
         * retriggering full-volume white noise continuously. Pulse1/noise
         * carry weapon/impact timbre; pulse2 retains UI and material cues. */
        static const uint16_t pitches[7][3]={{132,180,240},{250,300,360},{88,126,180},{480,600,740},{420,480,540},{120,82,56},{360,480,620}};
        static const uint8_t pulse_volume[7][3]={{3,2,0},{4,2,0},{4,2,0},{2,1,0},{1,1,0},{3,2,0},{2,1,0}};
        static const uint8_t noise_volume[7][3]={{2,1,0},{3,1,0},{2,0,0},{8,4,1},{2,1,0},{0,0,0},{3,2,0}};
        static const uint8_t noise_period[7]={4,7,0x83,12,8,0,10};
        unsigned index=(shot_kind==3?5u:3u)-accent_ticks;if(index>2)index=2;
        uint32_t amplitude=noise_volume[shot_kind][index];
        pulse(0,pitches[shot_kind][index],pulse_volume[shot_kind][index]);
        WRITE(12,0x10u|amplitude);WRITE(14,noise_period[shot_kind]);if(amplitude)WRITE(15,0x18);
        if(!--accent_ticks){WRITE(0,0xb0);WRITE(12,0x30);}
    }
}
