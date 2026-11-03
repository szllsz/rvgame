/* Normal mode uses live input and ordinary rules. Explicit Title-B comparison
 * uses labelled fixed ROM scenes/replays; it never supplies normal-game input. */
#include <stdint.h>
#include <stddef.h>
#include "touhou_game.h"
#include "touhou_gfx.h"
#include "touhou_sound.h"
#include "touhou_comparison.h"
#ifdef SCARLET_PRESENTATION
#include "scarlet_presentation.h"
#endif
#include "platform_input.h"
#include "input_tone.h"
#include "native800_frontend_boot.h"
#include "native800_frontend_assets.h"
#include "bt_mailbox.h"
#include "../load_capacity_20261029/graphics/fps_history.h"
#define DRAW(o) (*(volatile uint32_t *)(uintptr_t)(0x10000200u+(o)))
#define STATE(o) (*(volatile uint32_t *)(uintptr_t)(0x10000100u+(o)))
#define APU(o) (*(volatile uint32_t *)(uintptr_t)(0x10000300u+(o)))
#define game_wait th_wait
#include "../load_capacity_20261029/graphics/capacity_meter.h"
static ThGame game;
static unsigned frontend_request;
static unsigned frontend_exit_audio_wait;static uint32_t frontend_exit_at;
static ThComparison comparison,render_view;
_Static_assert(sizeof(ThComparison)==128,"Diagnostic bank comparison layout");
PlatformInput platform_controller;
static InputConnection connection;
static InputTone tone;
static ThSound sound;
static BtMailbox mailbox;
static uint32_t frames,logic_cycles,draw_cycles,copy_cycles,stack_peak,stack_bad,audio_stopped;
static uint32_t meter_have,meter_previous_tick,meter_previous_scan,meter_scans,meter_nonunit,meter_missed,meter_max_gap;
static uint32_t missed_logic,catchup_ticks,work_max,logic_max,draw_max,rt_pressed,lt_pressed;
static uint32_t logic_histogram[64],custom[64],frame_ticks,frame_hash,diagnostic_frozen,diagnostic_bank,measurement_seq;
static uint32_t rendered_phase,rendered_perf;
static uint32_t audio_volume=3,audio_muted=1,audio_previous;
#ifdef SCARLET_PRESENTATION
static uint32_t music_control;
#endif
static uint64_t meter_cycles;
static uint32_t *canary_end;
extern uint32_t __bss_end[],__stack_top[];
void *memset(void *p,int value,size_t n){uint8_t *d=p;while(n--)*d++=(uint8_t)value;return p;}
static void page(unsigned value){STATE(4)=value;STATE(20)=1;}
static void fail(void){
    /* Cancel native frame ownership before PS HOLD can wait on frame busy.
     * This read orders the posted cancel; DMA still drains in hardware. */
    DRAW(12)=10u;(void)DRAW(132);
    th_sound_quiet(&sound);INPUT_APU_WRITE(4,0xb0);
#ifdef SCARLET_PRESENTATION
    if(music_control){APU(0x94)=3;music_control=3;}
#endif
    page(0x3ff);for(;;){}
}
static void stack_init(void){
    uintptr_t sp;__asm__ volatile("mv %0, sp":"=r"(sp));canary_end=(uint32_t *)(sp-256);
    for(uint32_t *p=__bss_end;p<canary_end;++p)*p=0xa55ab67eu;
}
static void stack_check(void){
    uint32_t *p=__bss_end;while(p<canary_end&&*p==0xa55ab67eu)++p;
    stack_peak=(uint32_t)((uintptr_t)__stack_top-(uintptr_t)p);
    stack_bad=(uintptr_t)p<(uintptr_t)__bss_end+256;
    if(stack_bad)fail();
}
static void telemetry(uint32_t ack,int final){
    if(!bt_mailbox_advance(&mailbox,ack,final)){STATE(0)=bt_mailbox_symbol(&mailbox);return;}
    if(final&&(ack&4u)) {
        bt_mailbox_retirement(&mailbox,3u,audio_volume|(audio_muted<<8),frontend_request,
                              stack_bad|game.shot_overflow|sc_demo_state.fault|(DRAW(16)&8u));
        STATE(0)=bt_mailbox_symbol(&mailbox);return;
    }
    for(unsigned i=0;i<80;i++)mailbox.words[i]=0;
    uint32_t *w=mailbox.words;
    if(diagnostic_frozen&&!final){
        unsigned bank=diagnostic_bank;
        w[0]=0x54474231;w[1]=frames;w[2]=game.phase;w[3]=game.clock;w[4]=game.world.frame;
        w[5]=measurement_seq;w[6]=comparison.enabled;w[7]=bank;w[8]=comparison.software;
        w[9]=comparison.scene;w[10]=comparison.groups;w[11]=stack_peak;w[12]=stack_bad;w[13]=DRAW(16);w[14]=custom[2];w[15]=~frames;
        for(unsigned i=0;i<64;i++){
            if(bank<4)w[16+i]=cap_words[bank*64+i];
            else if(bank==4)w[16+i]=logic_histogram[i];
            else if(bank==5)w[16+i]=custom[i];
            else if(bank==6){
                const uint8_t *p=i<32?(const uint8_t *)&comparison:(const uint8_t *)&render_view;unsigned at=(i%32)*4;
                w[16+i]=(uint32_t)p[at]|((uint32_t)p[at+1]<<8)|((uint32_t)p[at+2]<<16)|((uint32_t)p[at+3]<<24);
            }
        }
        if(bank==7){
            w[16]=0x54414231;w[17]=STATE(36);w[18]=APU(0x90);w[19]=APU(0x80);w[20]=APU(0x84);w[21]=APU(0x88);w[22]=APU(0x8c);
            w[23]=DRAW(100);w[24]=sizeof(game);w[25]=sizeof(comparison);w[26]=sizeof(sound);
            w[27]=(uint32_t)((uintptr_t)__stack_top-(uintptr_t)__bss_end);w[28]=stack_peak;w[29]=w[27]-stack_peak;
            w[30]=stack_bad;w[31]=TH_ROI_W|(TH_ROI_H<<16);w[32]=frames;w[33]=rendered_phase;w[34]=rendered_perf;
        }
        bt_mailbox_seal(&mailbox);STATE(0)=bt_mailbox_symbol(&mailbox);return;
    }
    w[0]=0x54475031;w[1]=frames;w[2]=game.phase;w[3]=game.clock;w[4]=game.world.frame;w[5]=(uint32_t)game.lives;
    w[6]=game.shot_active;w[7]=game.world.active;w[8]=logic_cycles;w[9]=draw_cycles;w[10]=copy_cycles;
    w[11]=stack_peak;w[12]=stack_bad;w[13]=DRAW(16);w[14]=platform_controller.buttons;w[15]=~frames;
    w[16]=(uint32_t)game.player.x;w[17]=(uint32_t)game.player.y;w[18]=(uint32_t)game.player.vx;w[19]=(uint32_t)game.player.vy;
    w[20]=game.player.graze;w[21]=game.player.points;w[22]=game.damage_events;w[23]=game.death_events;
    w[24]=(uint32_t)game.death_at;w[25]=(uint32_t)game.respawn_at;w[26]=(uint32_t)game.recovery_at;
    union{float f;uint32_t u;} hp={game.boss_hp},maxhp={game.boss_max_hp};w[27]=hp.u;w[28]=maxhp.u;
    w[29]=game.shots_spawned;w[30]=game.shots_hit;w[31]=game.shots_retired;w[32]=game.shot_overflow;
    w[33]=game.world.spawned;w[34]=game.world.turned;w[35]=game.world.retired;w[36]=game.world.hit_removed;w[37]=game.world.cleared;
    w[38]=game.world.overflow;w[39]=game.world.ended;w[40]=game.timeout;w[41]=game.events;w[42]=!!(platform_controller.buttons&0x10000u);
    w[43]=game.power;w[44]=platform_controller.left;w[45]=platform_controller.right;w[46]=platform_controller.aux;
    w[47]=0x41550000u|audio_volume|(audio_muted<<8);
    w[48]=(uint32_t)meter_cycles;w[49]=(uint32_t)(meter_cycles>>32);w[50]=frames;w[51]=meter_scans;
    w[52]=meter_nonunit;w[53]=meter_missed;w[54]=meter_max_gap;w[55]=missed_logic;
    w[56]=work_max;w[57]=logic_max;w[58]=draw_max;w[59]=game.world.peak;w[60]=game.shot_peak;
    w[61]=DRAW(28);w[62]=STATE(24);w[63]=full_fps.valid;w[64]=full_fps.fps100[0];w[65]=full_fps.fps100[1];
    w[66]=rt_pressed;w[67]=lt_pressed;w[68]=game.seed;
    w[69]=game.attack_index;w[70]=game.attack_completed;w[71]=game.timeout_mask;w[72]=game.kill_mask;w[73]=game.rng.draws;
#ifdef SCARLET_PRESENTATION
    /* Presentation ABI binds expected source-pool rejects and rule flags;
     * ordinary gameplay retains its existing fields in non-Scarlet builds. */
    w[69]=sc_demo_state.rejected;w[71]=sc_demo_state.fault;
    w[72]=game.invincible|((uint32_t)game.boss_immortal<<1);
#endif
    w[74]=frontend_request?0x46520000u|frontend_request:!comparison.enabled&&game.phase==TH_TITLE?0x46510000u:sc_demo_paused?0x46530000u|(sc_demo_pause_choice&3u)|(audio_volume<<2)|(audio_muted<<6)|(comparison.groups<<7)|(comparison.scene<<11):game.entry_elapsed;
    w[75]=comparison.enabled;w[76]=comparison.software;w[77]=comparison.scene;w[78]=comparison.groups;w[79]=comparison.cursor;
    bt_mailbox_seal(&mailbox);STATE(0)=bt_mailbox_symbol(&mailbox);
}
static void finish_exit_audio(void){
    if(music_control!=3&&(!frontend_exit_audio_wait||(int32_t)(STATE(24)-frontend_exit_at)>=0)){
        APU(0x94)=3;music_control=3;frontend_exit_audio_wait=0;
    }
}
static void frozen(uint32_t ack){
    int final=(ack&2u)!=0;
    if(final&&mailbox.final&&mailbox.done){finish_exit_audio();return;}
    if(final&&mailbox.final&&((ack&1u)!=mailbox.phase))return;
    while(STATE(20)&1u){}
    if((ack&6u)==4u||diagnostic_frozen){
        diagnostic_frozen=1;cap_active=0;
        if(!final){input_poll(&platform_controller);diagnostic_bank=platform_controller.right&7u;}
    }
    stack_check();
    if(final||diagnostic_frozen)telemetry(ack,final);
    /* Publish each sealed final symbol through the existing frame commit.
     * The owner retains full CRC/ACK and retirement barriers. */
    if(final){if(mailbox.done)finish_exit_audio();STATE(4)=0x35f;STATE(20)=1;return;}
    page(final?0x35f:diagnostic_frozen?0x3ce:0x35e);
    while(STATE(20)&1u){}
}
static uint32_t controls(int32_t *x,int32_t *y){
    unsigned valid=input_poll(&platform_controller);uint32_t now=DRAW(80);
    if(native_boot_packet()){
        platform_controller.buttons=platform_controller.left=platform_controller.right=platform_controller.aux=0;
        *x=*y=0;return 0;
    }
    if(valid)input_tone_tick(&tone,input_connection(&connection,platform_controller.buttons&0x10000u,now),now);
    else input_tone_tick(&tone,0,now);
    input_movement(&platform_controller,x,y);
    if(!(platform_controller.aux&0x80000000u)){*x=(*x/256)*32767;*y=(*y/256)*32767;}
    uint32_t buttons=platform_controller.buttons&4095u;
    if(!(platform_controller.buttons&0x10000u)){rt_pressed=lt_pressed=0;return 0;}
    if(platform_controller.aux&0x80000000u){
        unsigned lt=platform_controller.aux&255u,rt=(platform_controller.aux>>8)&255u;
        if(rt>=77)rt_pressed=1;else if(rt<=51)rt_pressed=0;
        if(lt>=77)lt_pressed=1;else if(lt<=51)lt_pressed=0;
    }else rt_pressed=lt_pressed=0;
    if(rt_pressed)buttons|=TH_A;
    if(lt_pressed)buttons|=TH_X;
    return buttons;
}
static unsigned frontend(uint32_t buttons,int32_t x,int32_t y,unsigned steps,uint32_t scan){
    frame_ticks=0;
    uint32_t raw=platform_controller.buttons&4095u,pressed_audio=raw&~audio_previous;audio_previous=raw;
    if(raw&TH_SELECT){frontend_exit_audio_wait=0;frontend_request=0;}
#ifdef SCARLET_PRESENTATION
    uint32_t mute_button=TH_Y; /* Physical X under the current publisher ABI. */
#else
    uint32_t mute_button=TH_X;
#endif
    if((!comparison.enabled&&(game.phase==TH_TITLE||game.phase==TH_PAUSE))
#ifdef SCARLET_PRESENTATION
       ||comparison.enabled
#endif
    ){
        /* A simultaneous shoulder chord belongs to invincibility, including
         * at volume zero. Single shoulders retain the existing volume keys. */
        unsigned chord=(raw&(TH_LB|TH_RB))==(TH_LB|TH_RB);
        uint32_t volume_direction=0;
#ifdef SCARLET_PRESENTATION
        if(comparison.enabled&&sc_demo_paused&&sc_demo_pause_choice==0)volume_direction=pressed_audio&0xc0u;
#endif
        if(!chord&&(pressed_audio&TH_LB)&&audio_volume)audio_volume--;
        if(!chord&&(pressed_audio&TH_RB)&&audio_volume<8)audio_volume++;
        if((volume_direction&64u)&&audio_volume)audio_volume--;
        if((volume_direction&128u)&&audio_volume<8)audio_volume++;
        /* X is focus in play. Only physical X on the paused screen mutes;
         * LT's focus alias never changes the global audio preference. */
        if((game.phase==TH_PAUSE
#ifdef SCARLET_PRESENTATION
            ||comparison.enabled
#endif
           )&&(pressed_audio&mute_button))audio_muted^=1;
        if((pressed_audio&(1024u|2048u|mute_button))||volume_direction){APU(0x80)=audio_volume;APU(0x84)=audio_muted;}
    }
    if(!comparison.enabled&&game.phase==TH_TITLE){
        uint32_t pressed=raw&~game.previous_buttons;game.previous_buttons=raw;
        if(pressed&TH_SELECT){game.phase=TH_EXIT;return TH_COMPARE_EXIT;}
        /* No fixed sound-tail delay: pause as soon as retirement is acknowledged. */
        if(pressed&1u){frontend_exit_audio_wait=1;frontend_exit_at=scan;game.phase=TH_EXIT;return TH_COMPARE_EXIT;}
        if(pressed&2u){frontend_request=2;frontend_exit_audio_wait=1;frontend_exit_at=scan;game.phase=TH_EXIT;return TH_COMPARE_EXIT;}
        frame_hash=th_comparison_hash(&game);return 0;
    }
    if(comparison.enabled){
        uint32_t comparison_buttons=platform_controller.buttons&4095u;
#ifdef SCARLET_PRESENTATION
        if(lt_pressed)comparison_buttons|=SC_LT;
        if(rt_pressed)comparison_buttons|=SC_RT;
#endif
        unsigned flags=th_comparison_step(&comparison,&game,comparison_buttons);
        if((flags&TH_COMPARE_EXIT)&&!(raw&TH_SELECT)){frontend_request=1;frontend_exit_audio_wait=1;frontend_exit_at=scan;}
#ifdef SCARLET_PRESENTATION
        if(!(flags&(TH_COMPARE_RESET|TH_COMPARE_EXIT|TH_COMPARE_ERROR)))sc_demo_move(&comparison,&game,x,y,0);
#endif
#ifdef SCARLET_PRESENTATION
        /* Keep title/settings feedback audible through the same PCM stream.
         * Only a real exit pauses it before the existing CRC retirement. */
        uint32_t command=(flags&TH_COMPARE_EXIT)&&!frontend_exit_audio_wait?3u:1u;
        if(command!=music_control){APU(0x94)=command;music_control=command;}
#endif
        if(flags&TH_COMPARE_EXIT)th_sound_init(&sound,scan);
        else frame_ticks=(flags&TH_COMPARE_RESET)?0:1;
        frame_hash=comparison.enabled?comparison.hash:th_comparison_hash(&game);
        return flags;
    }
    uint32_t phase=game.phase,events=0;
    for(unsigned i=0;i<steps;i++){th_game_tick(&game,x,y,buttons);events|=game.events;frame_ticks++;}
    th_sound_tick(&sound,scan,events);
    frame_hash=th_comparison_hash(&game);
    return phase==TH_TITLE&&game.phase==TH_ENTRY?TH_COMPARE_RESET:0;
}
static void reset_measurement(void){
    cap_reset();cap_config(comparison.enabled?comparison.groups:1,comparison.enabled?comparison.software:0,comparison.scene,2);
    for(unsigned i=0;i<64;i++)logic_histogram[i]=custom[i]=0;
    custom[2]=2166136261u;custom[6]=custom[7]=custom[11]=custom[24]=0xffffffffu;missed_logic=catchup_ticks=0;measurement_seq++;
}
static void sum64(unsigned index,uint32_t value){uint32_t old=custom[index];custom[index]+=value;if(custom[index]<old)custom[index+1]++;}
static void measure_work(uint32_t work){
    cap_work(work,draw_cycles,comparison.enabled?0x10000u|(comparison.scene<<8)|(comparison.groups<<4)|comparison.software:game.phase);
    unsigned bin=logic_cycles>>15;if(bin>63)bin=63;logic_histogram[bin]++;
    custom[0]++;custom[2]=comparison.enabled?comparison.hash:(custom[2]^frame_hash)*16777619u;
    sum64(3,logic_cycles);if(logic_cycles>custom[5])custom[5]=logic_cycles;if(logic_cycles<custom[6])custom[6]=logic_cycles;
    if(game.world.active<custom[7])custom[7]=game.world.active;
    if(game.world.active>custom[8])custom[8]=game.world.active;
    sum64(9,game.world.active);
    if(game.shot_active<custom[11])custom[11]=game.shot_active;
    if(game.shot_active>custom[12])custom[12]=game.shot_active;
    sum64(13,game.shot_active);
    sum64(15,th_render_stats.attempted);sum64(17,th_render_stats.submitted);custom[19]+=th_render_stats.fully_clipped;custom[20]+=th_render_stats.alpha_submitted;
    custom[21]=game.world.overflow;custom[22]=game.shot_overflow;custom[23]=missed_logic;
    custom[29]=logic_cycles;custom[30]=draw_cycles;custom[32]+=frame_ticks;custom[33]=comparison.enabled?comparison.scene*16+comparison.groups:0xffffffffu;
    custom[34]=comparison.enabled?comparison.software:0;custom[35]=comparison.enabled?comparison.groups:1;custom[36]=comparison.enabled;
    custom[37]=game.rng.draws;custom[38]=game.seed;custom[39]=game.attack_index;custom[40]=game.world.frame;custom[41]=game.clock;
    custom[42]=frame_hash;custom[44]=th_render_stats.fully_clipped;custom[45]=th_render_stats.attempted;custom[46]=th_render_stats.submitted;custom[47]=th_render_stats.alpha_submitted;
    custom[48]=catchup_ticks;
#ifdef SCARLET_PRESENTATION
    custom[28]=sc_demo_paused?(1u+2u*sc_demo_pause_choice):0;
    custom[49]=sc_demo_state.rejected;custom[50]=sc_demo_state.cycles;
    custom[51]=sc_demo_state.trail_calls;custom[52]=sc_demo_state.release_calls;custom[53]=sc_demo_state.fault;
    custom[54]=APU(0x9c);custom[55]=APU(0xa0);custom[56]=APU(0xa8);
    custom[57]=APU(0x98);custom[58]=APU(0xac);custom[59]=APU(0xa4);
    custom[60]=APU(0xb4);custom[61]=APU(0xb8);custom[62]=APU(0xbc);custom[63]=APU(0xc0);
#endif
}
int main(void){
    if(DRAW(100)!=(300u<<16|800u)||DRAW(128)!=(600u<<16|800u))fail();
    uint32_t boot_audio=native_boot_audio();audio_volume=boot_audio&15u;audio_muted=(boot_audio>>8)&1u;
    APU(0x80)=audio_volume;APU(0x84)=audio_muted;
    int32_t x,y;uint32_t initial=controls(&x,&y);th_game_init(&game,20261031,initial);
#ifdef SCARLET_PRESENTATION
    th_comparison_enter(&comparison,platform_controller.buttons&4095u);
    if(!native_boot_demo()){
        sc_demo_init(&sc_demo_state,&game,2026);comparison.enabled=0;game.phase=TH_TITLE;game.previous_buttons=initial;
    }
    if(APU(0xa4)!=0x50434d31u||APU(0xb0)!=32000u)fail();
    APU(0x94)=1;music_control=1;
#endif
    input_tone_init();th_sound_init(&sound,STATE(24));full_fps_reference();stack_init();reset_measurement();uint32_t last_scan=STATE(24);
    for(;;){
        uint32_t ack=STATE(28);
        if((ack&6u)||game.phase==TH_EXIT||mailbox.final||diagnostic_frozen){
            if(!audio_stopped){
                th_sound_quiet(&sound);INPUT_APU_WRITE(4,0xb0);audio_stopped=1;
#ifdef SCARLET_PRESENTATION
                /* Diagnostics/errors pause now. Healthy UI sound tails run
                 * during CRC transport; final ACK then pauses before HOLD. */
                if(!frontend_exit_audio_wait||(ack&6u)==4u||diagnostic_frozen){APU(0x94)=3;music_control=3;}
#endif
            }
            frozen(ack);continue;
        }
        uint32_t scan=STATE(24),steps=scan-last_scan;if(!steps)continue;last_scan=scan;
        uint32_t begin=DRAW(80),buttons=controls(&x,&y);
        unsigned previous_perf=comparison.enabled;
        unsigned flags=frontend(buttons,x,y,steps>4?4:steps,scan);
        logic_cycles=DRAW(80)-begin;
        if(flags&TH_COMPARE_ERROR)fail();
        if(flags&TH_COMPARE_RESET){reset_measurement();if(comparison.enabled&&logic_cycles>comparison.reset_max_cycles)comparison.reset_max_cycles=logic_cycles;}
        if(!previous_perf&&!comparison.enabled&&steps>1){
            catchup_ticks+=(steps>4?4:steps)-1;
            if(steps>4)missed_logic+=steps-4;
        }
        if(game.phase==TH_EXIT)continue;
        th_gfx_mode(comparison.enabled?comparison.software:0);rendered_phase=game.phase;rendered_perf=comparison.enabled;
        draw_cycles=copy_cycles=0;
        if(!th_native_begin())fail();
        render_view=comparison;
        /* Both bands render one immutable logical state. Only a complete
         * two-band commit increments frames and the existing performance meter. */
        for(unsigned band=0;band<2;band++){
        th_native_band(band);begin=DRAW(80);
#ifdef SCARLET_PRESENTATION
        if(!comparison.enabled){th_fill(0,0,800,600,0);th_bitmap(sc_frontend_frames[0],800,800,600,0,0,4);}
        else sc_render_demo(&sc_demo_state,&game,&render_view,sc_demo_paused?(1u+2u*sc_demo_pause_choice):0,audio_volume,audio_muted);
#else
        if(comparison.enabled){render_view=comparison;th_render_comparison(&game,&render_view);}else {th_render_game(&game);th_render_audio(&game,audio_volume,audio_muted);}
#endif
        th_wait();draw_cycles+=DRAW(80)-begin;
        begin=DRAW(80);if(!th_native_flush(band))fail();copy_cycles+=DRAW(80)-begin;
        }
        uint32_t work=logic_cycles+draw_cycles;
        if(work>work_max)work_max=work;
        if(logic_cycles>logic_max)logic_max=logic_cycles;
        if(draw_cycles>draw_max)draw_max=draw_cycles;
        measure_work(work);
        frames++;cap_present();custom[31]=copy_cycles;
        uint32_t now=DRAW(80),current_scan=STATE(24);
        if(!custom[27]){custom[25]=now;custom[26]=DRAW(28);custom[27]=1;}
        else if(now-custom[25]>=105000000u){
            uint32_t fps=(uint32_t)((uint64_t)(DRAW(28)-custom[26])*105000000u*100u/(now-custom[25]));
            if(fps<custom[24])custom[24]=fps;
            custom[25]=now;custom[26]=DRAW(28);custom[27]++;
        }
        if(comparison.enabled&&th_comparison_present(&comparison,(uint64_t)cap_words[3]|((uint64_t)cap_words[4]<<32),cap_words[13],cap_words[17],cap_words[12])&TH_COMPARE_ERROR)fail();
        if(meter_have){uint32_t gap=current_scan-meter_previous_scan;meter_cycles+=now-meter_previous_tick;meter_scans+=gap;
            if(gap!=1)meter_nonunit++;
            if(gap>1)meter_missed+=gap-1;
            if(gap>meter_max_gap)meter_max_gap=gap;}
        meter_have=1;meter_previous_tick=now;meter_previous_scan=current_scan;
        full_fps_observe(now,DRAW(28),0,0,0,2,game.phase!=TH_PLAY,0);
        if(
#ifdef SCARLET_PRESENTATION
           game.world.overflow!=sc_demo_state.rejected||sc_demo_state.fault||
#else
           game.world.overflow||
#endif
           game.shot_overflow||(DRAW(16)&8u)||(DRAW(120)&2u)||__bss_end[0]!=0xa55ab67eu||__bss_end[63]!=0xa55ab67eu)fail();
        (void)cap_control(0,game.world.frame);
        while(STATE(20)&1u){}
        telemetry(ack,0);page(comparison.enabled?(sc_demo_paused?0x3e8u:0x3e0u)+comparison.software:0x3d0u+game.phase);
    }
}
