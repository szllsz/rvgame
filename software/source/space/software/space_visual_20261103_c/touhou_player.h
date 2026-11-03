/* Selected Taisei Reimu movement and projectile hit/graze geometry.
 * Full playable life/shooting/state layer is added after the G5 probe. */
#ifndef TOUHOU_PLAYER_H
#define TOUHOU_PLAYER_H
#include "touhou_model.h"
typedef struct ThPlayer {
    int32_t x,y,vx,vy;
    uint32_t hit_tests,graze_tests,hits,graze,points;
    uint8_t focused;
} ThPlayer;
void th_player_init(ThPlayer *p);
void th_player_move(ThPlayer *p,int32_t qx,int32_t qy,unsigned focus);
/* Source ellipse axes are full diameters, not radii. Coordinates are Q21. */
int th_segment_ellipse(int32_t ax,int32_t ay,int32_t bx,int32_t by,
                      uint32_t angle,int32_t diameter_x_q12,int32_t diameter_y_q12);
unsigned th_player_collision(void *context,ThBullet *bullet,int32_t old_x,int32_t old_y);
#endif
