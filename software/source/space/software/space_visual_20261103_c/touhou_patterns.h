/* Stage1 Normal nonspell1 gameplay stream, Taisei1.4.6 MIT.
 * Standalone G7 module; not yet linked into the qualified G6 normal binary. */
#ifndef TOUHOU_PATTERNS_H
#define TOUHOU_PATTERNS_H
#include "touhou_model.h"
#include "touhou_math.h"
enum { TH_NON_SPELL=1,TH_CRYSTAL_RAIN=2,TH_BALL_BLUE=3,TH_CRYSTAL_RANDOM=4,TH_BALL_RADIAL0=5,
       TH_RAIN_DROP=11,TH_RAIN_BIG=12 };
typedef struct {
    int32_t boss_vx,target_x,target_y,attraction;
    uint32_t id,wander_calls,wander_fallbacks,reserved;
} ThPattern;
_Static_assert(sizeof(ThPattern)==32,"Attack control ABI");
void th_pattern_init(ThWorld *w,ThPattern *p,ThRng *rng,uint32_t seed);
void th_rain_init(ThWorld *w,ThPattern *p);
void th_pattern_update(ThWorld *w,ThPattern *p,ThCollision collision,void *context);
void th_pattern_emit(ThWorld *w,ThPattern *p,ThRng *rng,int32_t player_x,int32_t player_y);
#endif
