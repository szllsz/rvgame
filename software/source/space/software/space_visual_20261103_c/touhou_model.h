/* Fixed-point adaptation of Taisei 1.4.6 Stage 1 Icicle Cascade.
 * Upstream pattern: MIT, Lukas Weber and Andrei Alexeyev, 2011-2026.
 * See COPYING.upstream and the pinned source receipt. No upstream engine code
 * or coroutine stacks are linked into the RV32 program.
 */
#ifndef TOUHOU_MODEL_H
#define TOUHOU_MODEL_H
#include <stdint.h>

enum { TH_CAPACITY=2048, TH_NONE=65535, TH_Q=2097152,
       TH_ATTACK_DELAY=60, TH_ATTACK_TICKS=2400 };

/* Signed 21-fraction-bit logical 480x560 coordinates (range -1024..1024).
 * Gray tx/ty hold the asymptotic target velocity. angle is a Q0.32 turn.
 * List order is birth order, even after reuse. Graze fields are countdowns. */
typedef struct {
    int32_t x,y,vx,vy,tx,ty;
    uint32_t id,angle;
    uint16_t age,next;
    uint8_t graze_cooldown,graze_reset,kind,graze_count;
} ThBullet;
_Static_assert(sizeof(ThBullet)==40,"RV32 bullet ABI");

typedef struct ThWorld {
    ThBullet bullets[TH_CAPACITY];
    int32_t boss_x,boss_y,boss_vy;
    uint32_t frame,next_id,spawned,turned,retired,cleared,overflow,hit_removed;
    uint16_t head,tail,free_head,active,peak,round;
    uint8_t ended,emitter_groups;
} ThWorld;

void th_init(ThWorld *w);
void th_tick(ThWorld *w);
/* Return 0=none, 1=fatal hit, 2=graze. Collision precedes OOB and turn. */
typedef unsigned (*ThCollision)(void *context,ThBullet *bullet,int32_t old_x,int32_t old_y);
void th_tick_checked(ThWorld *w,ThCollision collision,void *context);
/* Player shoot/death/respawn tasks run between old-projectile processing and
 * attack tasks in the source scheduler. The game layer uses these phases. */
void th_update_checked(ThWorld *w,ThCollision collision,void *context);
void th_emit(ThWorld *w);
void th_clear(ThWorld *w);
int32_t th_sin(uint32_t angle);
int32_t th_cos(uint32_t angle);
int32_t th_mul_q30(int32_t a,int32_t b);
#endif
