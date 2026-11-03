/* CHR/MMIO adaptation independently implemented for rvgame.
 * Format reference: SMB-Vanilla src/platform/render_raster.c.
 * No game core or third-party palette data is linked here.
 */
#include "game_gfx.h"
#include "native800_frame.h"
#define DRAW(offset) (*(volatile uint32_t *)(uintptr_t)(0x10000200u + (offset)))
#ifdef NATIVE800_HOST
#undef DRAW
#define DRAW(offset) (*native800_host_reg(offset))
#endif
static uint8_t background;
static uint32_t origin_x, origin_y;
static uint32_t viewport_width=256,viewport_height=240;
int game_viewport(uint32_t width,uint32_t height)
{
    uint32_t geometry=DRAW(128);
    if(!width || !height || width>(geometry&65535u) || height>(geometry>>16)) return 0;
    viewport_width=width;viewport_height=height;
    unsigned layout=width==640 && height==480;
    native800_layout640(layout);
    origin_x=layout?0:((geometry&65535u)-width)/2;origin_y=layout?0:((geometry>>16)-height)/2;
    native800_offset((int)origin_x,(int)origin_y);return 1;
}

void game_wait(void)
{
    native800_wait();
}

int game_upload_chr(const uint8_t *chr, uint32_t first, uint32_t count)
{
    if (!chr || first >= 512 || !count || count > 512u - first) return 0;
    game_wait();
    DRAW(48) = first << 3;
    for (uint32_t t = 0; t < count; ++t) {
        for (uint32_t row = 0; row < 8; ++row) {
            uint32_t packed = 0;
            for (uint32_t x = 0; x < 8; ++x) {
                uint32_t lo = (chr[row] >> (7u - x)) & 1u;
                uint32_t hi = (chr[row + 8] >> (7u - x)) & 1u;
                packed |= (lo | (hi << 1)) << (x << 1);
            }
            DRAW(56) = packed;
        }
        chr += 16;
    }
    return 1;
}

int game_palette(const uint8_t *indices, const uint8_t *rgb)
{
    if (!indices || !rgb) return 0;
    game_wait();
    for (uint32_t i = 0; i < 32; ++i) {
        const uint8_t *color = rgb + (uint32_t)(indices[i] & 63u) * 3u;
        uint32_t packed = (color[0] & 0xe0u) | ((color[1] >> 3) & 0x1cu) | (color[2] >> 6);
        DRAW(52) = i;
        DRAW(60) = packed;
        if (i == 16) background = (uint8_t)packed;
    }
    return 1;
}

static void fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    native800_fill((int)x,(int)y,(int)w,(int)h,color);
}

void game_clear(uint8_t outside_color)
{
    // Full clear initializes the back bank; viewport clear alone cannot do so.
    uint32_t geometry = DRAW(128);
    uint32_t width = geometry & 65535u, height = geometry >> 16;
    unsigned layout=viewport_width==640 && viewport_height==480;
    origin_x = layout?0:(width - viewport_width) >> 1;
    origin_y = layout?0:(height - viewport_height) >> 1;
    native800_offset((int)origin_x,(int)origin_y);
    native800_clear(width==viewport_width && height==viewport_height?background:outside_color);
    native800_clip(0,0,(int)viewport_width,(int)viewport_height);
    if((width!=viewport_width || height!=viewport_height) && background!=outside_color)
        fill(0,0,viewport_width,viewport_height,background);
}

int game_tile(int32_t x, int32_t y, uint32_t tile, uint32_t palette,
              uint32_t flip_x, uint32_t flip_y)
{
    if (tile >= 512 || palette >= 8 || flip_x > 1 || flip_y > 1) return 0;
    // Reject entirely invisible positions before adding the viewport origin:
    // extreme int32 coordinates cannot overflow into visible pixels.
    if (x < -7 || x >= (int32_t)viewport_width || y < -7 || y >= (int32_t)viewport_height) return 1;
    native800_tile(x,y,tile,palette,flip_x,flip_y);
    return 1;
}
