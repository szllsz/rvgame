/* Temporary candidate runtime on the accepted platform: gameplay stays on RV32. */
#include "brotato_fidelity_display.h"
#include "arena_audio.h"
#include "game_gfx.h"
#include "native800_frame.h"
#include "bt_mailbox.h"
#include <stddef.h>
#include "platform_input.h"
#include "native_boot.h"
void arena_audio_connection(int event);
void arena_music_pause(void);
int bt_showcase_shift(TatoGame *game,unsigned up);
PlatformInput platform_controller;
static InputRepeat navigation;
static InputConnection connection;
#define DRAW(o) (*(volatile uint32_t *)(uintptr_t)(0x10000200u+(o)))
#define STATE(o) (*(volatile uint32_t *)(uintptr_t)(0x10000100u+(o)))
void *memset(void *pointer,int value,size_t length)
{unsigned char *p=pointer;while(length--)*p++=(unsigned char)value;return pointer;}
static TatoGame game;
static BtMailbox mailbox;
static uint32_t mailbox_aux;
static uint32_t render_frames,logic_cycles,draw_cycles,wait_cycles,stack_peak,stack_bad,previous,catchup_render_skips,catching_up;
static uint32_t options_origin=TATO_TITLE;
static unsigned showcase_lt,showcase_rt;
static uint64_t logic_sum,draw_sum,wait_sum;
/* Eight words each: epoch, population, presents, elapsed cycles lo/hi,
 * scans, completed frames, non-unit scan intervals. First two seconds warm up.
 * Previous stage stays visible long enough for the unchanged slow mailbox. */
static uint32_t stage_meter[8],stage_meter_previous[8];
static uint32_t meter_tick,meter_scan,meter_completed,meter_game_tick;
static void showcase_present(void)
{
    if(!game.showcase || game.mode!=TATO_PLAY)return;
    uint32_t population=bt_showcase_population(game.run.wave);
    if(stage_meter[1]!=population || game.ticks<meter_game_tick){
        for(unsigned i=0;i<8;i++){stage_meter_previous[i]=stage_meter[i];stage_meter[i]=0;}
        stage_meter[0]=stage_meter_previous[0]+1;stage_meter[1]=population;
    }
    meter_game_tick=game.ticks;
    if(game.ticks<120)return;
    uint32_t tick=DRAW(80),scan=STATE(24),completed=DRAW(28);
    if(stage_meter[2]){
        uint32_t old=stage_meter[3],delta=scan-meter_scan;
        stage_meter[3]+=tick-meter_tick;if(stage_meter[3]<old)++stage_meter[4];
        stage_meter[5]+=delta;stage_meter[6]+=completed-meter_completed;
        if(delta!=1)++stage_meter[7];
    }
    ++stage_meter[2];meter_tick=tick;meter_scan=scan;meter_completed=completed;
}
extern uint32_t __bss_end[],__stack_top[];
static uint32_t *canary_end;
static void stack_init(void)
{
    uintptr_t sp;__asm__ volatile("mv %0, sp":"=r"(sp));
    canary_end=(uint32_t *)(sp-256);
    for(uint32_t *p=__bss_end;p<canary_end;++p)*p=0xa55ab67eu;
}
static void stack_check(void)
{
    uint32_t *p=__bss_end;while(p<canary_end && *p==0xa55ab67eu)++p;
    stack_peak=(uint32_t)((uintptr_t)__stack_top-(uintptr_t)p);
    stack_bad=(uintptr_t)p<(uintptr_t)__bss_end+256;
    if(stack_bad)bt_world_fault(&game,1u<<20);
}
static void telemetry(uint32_t ack,int final)
{
    if(!bt_mailbox_advance(&mailbox,ack,final)) {
        STATE(0)=bt_mailbox_symbol(&mailbox);return;
    }
    if(final&&(ack&4u)) {
        bt_mailbox_retirement(&mailbox,2u,arena_audio_volume()|(arena_audio_muted()<<8),0u,
                              game.fault|game.missed_logic_ticks|stack_bad|(DRAW(16)&8u));
        STATE(0)=bt_mailbox_symbol(&mailbox);return;
    }
    if(!final && !game.showcase && (ack&4u) && (mailbox_aux^=1u)){
        /* Explicit owner test/diagnostic request, never the normal transport.
         * Original 80-word primary and final snapshots remain unchanged. */
        for(unsigned i=0;i<80;++i)mailbox.words[i]=0;
        mailbox.words[0]=0x494e5031u;mailbox.words[1]=render_frames;
        mailbox.words[2]=platform_controller.buttons;mailbox.words[3]=platform_controller.left;
        mailbox.words[4]=platform_controller.right;mailbox.words[5]=platform_controller.aux;
        int32_t move_x,move_y;input_movement(&platform_controller,&move_x,&move_y);
        mailbox.words[6]=(uint32_t)move_x;mailbox.words[7]=(uint32_t)move_y;
        mailbox.words[8]=!!((platform_controller.buttons&0x10000u) && (platform_controller.aux&0x80000000u));
        mailbox.words[9]=(uint32_t)game.x;mailbox.words[10]=(uint32_t)game.y;
        mailbox.words[11]=(uint32_t)game.last_dx;mailbox.words[12]=(uint32_t)game.last_dy;
        mailbox.words[13]=game.mode;mailbox.words[14]=stack_peak;mailbox.words[15]=~render_frames;
        bt_mailbox_seal(&mailbox);STATE(0)=bt_mailbox_symbol(&mailbox);return;
    }
    /* Auxiliary observations never replace an original snapshot or final.
     * Each normal packet still carries all original 80 words and checksum. */
    if(!final && game.showcase && (mailbox_aux^=1u)){
        uint32_t alive=0,births=0,shots=0,boss_alive=0,boss_birth=0;
        for(unsigned i=0;i<TATO_ENEMIES;++i){alive+=game.enemies[i].hp>0;births+=game.enemies[i].birth>0;
            if(bt_enemy_defs[game.enemies[i].kind].boss){boss_alive+=game.enemies[i].hp>0;boss_birth+=game.enemies[i].birth>0;}}
        for(unsigned i=0;i<BT_MOB_SHOTS;++i)shots+=game.mob_shots[i].active>0;
        for(unsigned i=0;i<80;++i)mailbox.words[i]=0;
        uint32_t extra[16]={BT_SHOWCASE_MAGIC,render_frames,4,game.run.wave,game.ticks,bt_showcase_population(game.run.wave),
            alive,births,game.spawn_count,shots,game.showcase_born,game.showcase_projectiles,
            BT_SHOWCASE_SEED,game.test_invincible,game.fault,~render_frames};
        for(unsigned i=0;i<16;++i)mailbox.words[i]=extra[i];
        /* Version4 adds all-present scan metering and keeps route/boss proof.
         * proof. The original primary 80 words below stay intact. */
        mailbox.words[16]=0x42575334u;mailbox.words[17]=game.wave;mailbox.words[18]=game.run.difficulty;
        mailbox.words[19]=boss_alive;mailbox.words[20]=boss_birth;mailbox.words[21]=(uint32_t)game.boss_count;
        mailbox.words[22]=(uint32_t)game.x;mailbox.words[23]=(uint32_t)game.y;
        mailbox.words[24]=(uint32_t)game.last_dx;mailbox.words[25]=(uint32_t)game.last_dy;mailbox.words[26]=1;
        for(unsigned i=0;i<8;i++){mailbox.words[27+i]=stage_meter[i];mailbox.words[35+i]=stage_meter_previous[i];}
        bt_mailbox_seal(&mailbox);STATE(0)=bt_mailbox_symbol(&mailbox);return;
    }
    volatile uint32_t *audio=(volatile uint32_t *)(uintptr_t)0x10000300u;
    /* Stream the identical 80 fields into the unpublished snapshot.
     * Avoid a second 320-byte automatic buffer before sealing/commit. */
    mailbox.words[0]=0x42545331u;mailbox.words[1]=render_frames;mailbox.words[2]=game.structure_peak;mailbox.words[3]=game.structure_overflows;
    mailbox.words[4]=game.structure_original_removals;mailbox.words[5]=game.structure_shots;mailbox.words[6]=game.mine_explosions;mailbox.words[7]=game.burn_hits;
    mailbox.words[8]=game.structure_healing;mailbox.words[9]=TATO_ENEMIES;mailbox.words[10]=TATO_BULLETS;mailbox.words[11]=BT_STRUCTURE_CAP;
    mailbox.words[12]=sizeof(BtStructure);mailbox.words[13]=sizeof(game);mailbox.words[14]=game.pacifist_bonus_total;mailbox.words[15]=~render_frames;
    mailbox.words[16]=0x53595331u;mailbox.words[17]=render_frames;mailbox.words[18]=game.mode|(game.test_invincible<<8);mailbox.words[19]=game.wave;
    mailbox.words[20]=game.ticks;mailbox.words[21]=game.audio_events|((DRAW(16)&15u)<<16);mailbox.words[22]=audio[32];mailbox.words[23]=audio[33];
    mailbox.words[24]=audio[34];mailbox.words[25]=audio[36];mailbox.words[26]=DRAW(88);mailbox.words[27]=STATE(24);
    mailbox.words[28]=game.kills;mailbox.words[29]=(uint32_t)game.run.hp;mailbox.words[30]=(uint32_t)game.run.gold;mailbox.words[31]=~render_frames;
    mailbox.words[32]=0x42544632u;mailbox.words[33]=game.fault;mailbox.words[34]=game.logic_total;mailbox.words[35]=game.normal_ticks;
    mailbox.words[36]=game.god_ticks;mailbox.words[37]=game.enemy_peak;mailbox.words[38]=game.bullet_peak;mailbox.words[39]=game.mob_peak;
    mailbox.words[40]=game.spawn_peak;mailbox.words[41]=game.birth_peak;mailbox.words[42]=game.drop_merges;mailbox.words[43]=game.entity_overflows;
    mailbox.words[44]=game.bullet_overflows;mailbox.words[45]=game.mob_overflows;mailbox.words[46]=game.spawn_overflows;mailbox.words[47]=game.group_overflows;
    mailbox.words[48]=game.original_population_removals;mailbox.words[49]=game.consumable_overflows;mailbox.words[50]=game.missed_logic_ticks;mailbox.words[51]=stack_peak;
    mailbox.words[52]=stack_bad;mailbox.words[53]=(uint32_t)sizeof(game);mailbox.words[54]=logic_cycles;mailbox.words[55]=draw_cycles;
    mailbox.words[56]=wait_cycles;mailbox.words[57]=(uint32_t)logic_sum;mailbox.words[58]=(uint32_t)(logic_sum>>32);mailbox.words[59]=(uint32_t)draw_sum;
    mailbox.words[60]=(uint32_t)(draw_sum>>32);mailbox.words[61]=(uint32_t)wait_sum;mailbox.words[62]=(uint32_t)(wait_sum>>32);mailbox.words[63]=game.damage_taken;
    mailbox.words[64]=game.damage_dealt;mailbox.words[65]=game.materials_collected;mailbox.words[66]=(uint32_t)game.run.bonus_gold;mailbox.words[67]=game.run.level;
    mailbox.words[68]=(uint32_t)game.run.xp10000;mailbox.words[69]=(uint32_t)((uint64_t)game.run.xp10000>>32);mailbox.words[70]=game.run.pending_upgrades;mailbox.words[71]=game.run.upgrade_peak;
    mailbox.words[72]=game.run.role;mailbox.words[73]=game.run.difficulty;mailbox.words[74]=game.run.endless;mailbox.words[75]=game.run.weapon_count;
    mailbox.words[76]=game.all_final_bosses_killed;mailbox.words[77]=0x41550000u|arena_audio_volume()|(arena_audio_muted()<<8);mailbox.words[78]=catchup_render_skips;mailbox.words[79]=~render_frames;
    bt_mailbox_seal(&mailbox);STATE(0)=bt_mailbox_symbol(&mailbox);
}
int main(void)
{
    if(DRAW(128)!=(600u<<16|800u)) {STATE(4)=0x3ff;STATE(20)=1;for(;;){}}
    volatile uint32_t *pcm=(volatile uint32_t *)(uintptr_t)0x10000300u;
    if(pcm[41]!=0x50434d31u || pcm[44]!=32000u) {STATE(4)=0x3ff;STATE(20)=1;for(;;){}}
    stack_init();tato_graphics_init();tato_init(&game);arena_audio_init();uint32_t last_scan=STATE(24);
    for(;;) {
        uint32_t ack=STATE(28);
        if((ack&2u) || game.mode==TATO_EXIT || mailbox.final) {
            arena_music_pause(); /* Preserve the final count/CRC until owner hold. */
            /* Explicit end-of-window freeze, not an unreported lost game tick.
             * Final counters stay immutable while the host receives every word. */
            /* A spontaneous Select first requests a host-synchronized ACK
             * epoch. Otherwise an in-flight old ACK can skip the final marker. */
            if(ack&2u){
                /* A committed symbol is immutable until its host ACK. Do not
                 * submit it again and spend a second frame waiting for ACK. */
                if(mailbox.final && (mailbox.done || (ack&1u)!=mailbox.phase))continue;
                /* STATE0/4 are shadow registers. Each token must commit at a
                 * frame edge before the host can acknowledge its live value. */
                if(STATE(20)&1u)continue;
                telemetry(ack,1);STATE(4)=0x35f;STATE(20)=1;continue;
            }
            else STATE(4)=0x35e;
            STATE(20)=1;
            while(STATE(20)&1u){}continue;
        }
        uint32_t begin=DRAW(80),scan=STATE(24),steps=scan-last_scan;last_scan=scan;
        if(!steps){catching_up=0;continue;}
        unsigned input_valid=input_poll(&platform_controller);
        unsigned boot_active=native_boot_packet()!=0;
        if(boot_active)platform_controller.buttons=platform_controller.left=platform_controller.right=platform_controller.aux=0;
        uint32_t buttons=platform_controller.buttons&0xfffu;
        if(input_valid&&!boot_active)arena_audio_connection(input_connection(&connection,platform_controller.buttons&0x10000u,begin));
        if(steps>1)catching_up=1;
        if(steps>4) {game.missed_logic_ticks+=steps-4;bt_world_fault(&game,1u<<21);steps=1;}
        uint32_t pressed=buttons&~previous;
        unsigned old_triggers=showcase_lt|(showcase_rt<<1);
        if(!boot_active && (platform_controller.buttons&0x10000u) && (platform_controller.aux&0x80000000u)){
            unsigned lt=platform_controller.aux&255u,rt=(platform_controller.aux>>8)&255u;
            if(lt>=77)showcase_lt=1;else if(lt<=51)showcase_lt=0;
            if(rt>=77)showcase_rt=1;else if(rt<=51)showcase_rt=0;
        }else showcase_lt=showcase_rt=0;
        unsigned triggers=showcase_lt|(showcase_rt<<1),edges=triggers&~old_triggers;
        if(game.mode!=TATO_PLAY)pressed=input_repeat(&navigation,buttons,pressed,begin,game.mode+1);
        else navigation.direction=navigation.context=0;
        /* Original-style START pause/options; A/B return to the actual origin,
         * including title and shop. Y remains the original information action. */
        if(game.mode!=TATO_PAUSE)options_origin=game.mode;
        if((pressed&8u) && game.mode!=TATO_FAULT && game.mode!=TATO_EXIT){
            game.mode=game.mode==TATO_PAUSE?options_origin:TATO_PAUSE;
            pressed&=~11u;
        }else if(game.mode==TATO_PAUSE && (pressed&11u)){
            game.mode=options_origin;pressed&=~11u;
        }
        arena_audio_controls(game.mode,pressed);
        if(game.showcase && game.mode==TATO_PLAY && triggers!=3u){
            if(edges&2u)bt_showcase_shift(&game,1);
            else if(edges&1u)bt_showcase_shift(&game,0);
        }
        tato_tick(&game,buttons,pressed);arena_audio_tick(&game);previous=buttons;
        for(uint32_t i=1;i<steps && game.mode==TATO_PLAY;++i) {tato_tick(&game,buttons,0);arena_audio_tick(&game);}
        logic_cycles=DRAW(80)-begin;logic_sum+=logic_cycles;
        if(game.mode==TATO_EXIT) {
            game_wait();continue;
        }
        /* Retire every due logic tick, then catch the next scan without another
         * draw/swap delay. This avoids a transient overrun locking into 30 FPS. */
        if(game.mode!=TATO_PLAY)catching_up=0;
        if(catching_up && game.mode==TATO_PLAY) {++catchup_render_skips;continue;}
        if(render_frames%60==0)stack_check();
        uint32_t draw_begin=DRAW(80),band_wait=0;native800_begin();
        for(unsigned band=0;band<2;band++){
            native800_band(band);tato_render(&game);game_wait();
            if(band==1)DRAW(88)=DRAW(80)-begin-band_wait;
            uint32_t copied=DRAW(80);native800_flush(band);band_wait+=DRAW(80)-copied;
        }
        draw_cycles=DRAW(80)-draw_begin-band_wait;draw_sum+=draw_cycles;
        ++render_frames;telemetry(ack,0);
        STATE(4)=0x350+game.mode;STATE(20)=1;
        wait_cycles=band_wait;wait_sum+=wait_cycles;
        showcase_present();
    }
}
