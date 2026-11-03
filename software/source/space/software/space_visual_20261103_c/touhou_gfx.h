#ifndef TOUHOU_GFX_H
#define TOUHOU_GFX_H
#include <stdint.h>
enum { TH_WIDTH=800,TH_HEIGHT=600,TH_ROI_X=256,TH_ROI_Y=176,TH_ROI_W=128,TH_ROI_H=128 };
typedef struct {uint32_t attempted,submitted,fully_clipped,alpha_submitted;} ThRenderStats;
extern ThRenderStats th_render_stats;
void th_gfx_mode(unsigned software);
void th_clip(int left,int top,int right,int bottom);
void th_fill(int x,int y,int width,int height,unsigned color);
void th_bitmap(const uint8_t *source,unsigned stride,unsigned width,unsigned height,int x,int y,unsigned alpha);
void th_wait(void);
void th_native_band(unsigned band);
int th_native_begin(void);
int th_native_flush(unsigned band);
typedef struct ThWorld ThWorld;
typedef struct ThPlayer ThPlayer;
typedef struct ThGame ThGame;
typedef struct ThComparison ThComparison;
void th_render(const ThWorld *world);
void th_render_scene(const ThWorld *world,const ThPlayer *player);
void th_render_game(const ThGame *game);
void th_render_audio(const ThGame *game,unsigned volume,unsigned muted);
void th_render_comparison(const ThGame *game,const ThComparison *comparison);
#endif
