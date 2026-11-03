#ifndef SCARLET_DEMO_H
#define SCARLET_DEMO_H
#include "touhou_game.h"
enum { SC_BUBBLE=21,SC_STILL=22,SC_MOVING=23,SC_SOURCE_POOL=640,SC_DENSITY_LEVELS=9,SC_DEFAULT_DENSITY=1 };
/* Native load tiers are calibrated on the final artwork; trigger edges use
 * private comparison bits, outside the existing 12-bit digital button ABI. */
enum { SC_TIER_LOW=1,SC_TIER_MID=2,SC_TIER_HIGH=9,SC_LT=4096,SC_RT=8192 };
static inline unsigned sc_density_tier(unsigned level){
    return level>=SC_TIER_HIGH?2u:level>=SC_TIER_MID?1u:0u;
}
typedef struct {
    uint32_t angle;
    int32_t counter0,counter3,int3;
    uint16_t ip,time;
    uint8_t child,reserved[3];
} ScContext;
typedef struct {
    ScContext current,saved;
    uint32_t tick,cycles,rng_draws,rejected,trail_calls,release_calls,fault;
    uint32_t boss_angle;
    int32_t boss_speed,move_x,move_y,move_dx,move_dy,axis_x,axis_y;
    int32_t effect_acceleration,effect_angle;
    uint16_t seed,next_slot,move_remaining,move_duration,effect_duration;
    uint8_t in_call,density;
} ScDemo;
/* Uses the local ECL's complete looping attack, not the old 600-frame scene.
 * Coordinates stay in original 384x448 logical space. Rendering maps them.
 * The pool and helper semantics currently follow the pinned TH06 CC0 engine;
 * New Classic-specific helper equivalence remains a separately reported limit.
 */
void sc_demo_init(ScDemo *s,ThGame *g,uint16_t seed);
void sc_demo_tick(ScDemo *s,ThGame *g);
void sc_demo_density(ScDemo *s,unsigned level);
unsigned sc_demo_pool(const ScDemo *s);
#endif
