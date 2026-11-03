/* Taisei short-level adaptation: ReimuA fixed rank0, no bombs/items/slaves.
 * Initial source power is100; selecting0 here is an explicit limited mode,
 * not a claim that the complete upstream player configuration is restored. */
#ifndef TOUHOU_GAME_H
#define TOUHOU_GAME_H
#include "touhou_model.h"
#include "touhou_player.h"
#include "touhou_math.h"
#include "touhou_patterns.h"
enum { TH_A=1,TH_B=2,TH_SELECT=4,TH_START=8,TH_X=256,TH_Y=512,TH_LB=1024,TH_RB=2048,
       TH_TITLE=0,TH_PLAY=1,TH_PAUSE=2,TH_OUTRO=3,TH_WIN=4,TH_LOSE=5,TH_EXIT=6,TH_BETWEEN=7,TH_ENTRY=8,
       TH_SHOT_POOL=128,TH_SHOT_Q=262144,
       TH_EVENT_SHOT=1,TH_EVENT_HIT=2,TH_EVENT_HURT=4,TH_EVENT_DEATH=8,
       TH_EVENT_WIN=16,TH_EVENT_LOSE=32,TH_EVENT_GRAZE=64 };
typedef struct {
    int32_t x,y,vx,vy;
    uint32_t id,angle;
    uint16_t age,damage,next;
    uint8_t kind,reserved;
} ThShot;
_Static_assert(sizeof(ThShot)==32,"Player projectile ABI");
typedef struct ThGame {
    ThWorld world;
    ThPlayer player;
    ThShot shots[TH_SHOT_POOL];
    ThRng rng;
    ThPattern attack;
    uint32_t attack_index,attack_completed,timeout_mask,kill_mask,entry_elapsed;
    uint32_t seed,clock,previous_buttons,phase,resume_phase,finish_at;
    int32_t lives,power,death_at,respawn_at,recovery_at;
    float boss_hp,boss_max_hp,damage_dealt;
    uint32_t damage_events,death_events,events,spell_failed,timeout;
    uint32_t next_shot_id,shots_spawned,shots_hit,shots_retired,shot_overflow;
    uint32_t forward_at,volley_at,forward_side;
    uint16_t shot_head,shot_tail,shot_free,shot_active,shot_peak;
    /* The two bytes formerly used as tail padding; ROM snapshot size stays
     * fixed. These flags are explicit adaptations, not upstream game rules. */
    uint8_t invincible,boss_immortal;
} ThGame;
_Static_assert(sizeof(ThGame)==45376+(TH_CAPACITY-1024)*sizeof(ThBullet),"Game snapshot ABI including mode flags and declared bullet pool");
void th_game_init(ThGame *g,uint32_t seed,uint32_t held_buttons);
void th_game_start(ThGame *g,uint32_t seed,uint32_t held_buttons);
void th_game_tick(ThGame *g,int32_t axis_x,int32_t axis_y,uint32_t buttons);
int th_game_alive(const ThGame *g);
void th_game_presentation_shots(ThGame *g);
#endif
