#ifndef ARENA_AUDIO_H
#define ARENA_AUDIO_H
#include "fidelity_world.h"
void arena_audio_init(void);
void arena_audio_controls(uint32_t mode,uint32_t pressed);
void arena_audio_tick(const TatoGame *game);
uint32_t arena_audio_volume(void);
uint32_t arena_audio_muted(void);
#endif
