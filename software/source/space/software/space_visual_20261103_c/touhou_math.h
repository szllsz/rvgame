#ifndef TOUHOU_MATH_H
#define TOUHOU_MATH_H
#include <stdint.h>
/* Angle in unsigned Q0.32 turns; input vectors keep their own fixed scale. */
uint32_t th_angle(int32_t x,int32_t y);
uint64_t th_root64(uint64_t value);
void th_normalize(int32_t x,int32_t y,int32_t magnitude,int32_t *out_x,int32_t *out_y);
typedef struct {uint64_t state[4];uint32_t draws;} ThRng;
void th_rng_init(ThRng *rng,uint64_t seed);
uint64_t th_rng_next(ThRng *rng);
int32_t th_random_signed_q30(uint64_t value);
uint32_t th_random_angle(uint64_t value);
/* Source nonspell1 explicitly narrows its ring offset to IEEE float32. This
 * rare conversion may link soft-float; all per-bullet motion stays fixed. */
uint32_t th_random_float_angle(uint64_t value);
#endif
