#ifndef TOUHOU_COLLISION_H
#define TOUHOU_COLLISION_H
#include "touhou_player.h"
/* Dispatch by actual upstream projectile prototype, not rendered dimensions. */
unsigned th_enemy_collision(void *context,ThBullet *bullet,int32_t old_x,int32_t old_y);
#endif
