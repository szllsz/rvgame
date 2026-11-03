#ifndef GAME_GFX_H
#define GAME_GFX_H
#include <stdint.h>

/* Caller supplies complete buffers: count*16 CHR bytes, 32 indices, 64 RGB triples.
 * Serialized MMIO interface; caller keeps upstream tile callback ordering.
 */
int game_upload_chr(const uint8_t *chr, uint32_t first, uint32_t count);
int game_palette(const uint8_t *indices, const uint8_t *rgb);
void game_clear(uint8_t outside_color);
int game_tile(int32_t x, int32_t y, uint32_t tile, uint32_t palette,
              uint32_t flip_x, uint32_t flip_y);
void game_wait(void);
int game_viewport(uint32_t width,uint32_t height);
#endif
