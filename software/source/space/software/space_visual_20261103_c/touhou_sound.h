/* Synthetic APU event approximations, not upstream PCM or background music. */
#ifndef TOUHOU_SOUND_H
#define TOUHOU_SOUND_H
#include <stdint.h>
typedef struct {
    uint32_t pulse_until,noise_until,shot_at,hit_at,graze_at;
    uint32_t sequence,step,next,pulse_active,noise_rank;
} ThSound;
void th_sound_init(ThSound *sound,uint32_t scan);
void th_sound_tick(ThSound *sound,uint32_t scan,uint32_t events);
void th_sound_quiet(ThSound *sound);
#endif
