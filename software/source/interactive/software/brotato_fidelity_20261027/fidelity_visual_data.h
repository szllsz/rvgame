#ifndef FIDELITY_VISUAL_DATA_H
#define FIDELITY_VISUAL_DATA_H
#include <stdint.h>
#include "fidelity_tables.h"
typedef struct {const uint8_t *pixels;int16_t width,height,x,y;} BtSprite;
extern const BtSprite bt_role_sprites[BT_ROLE_COUNT],bt_enemy_sprites[32],bt_weapon_sprites[BT_WEAPON_KINDS],bt_item_sprites[BT_ITEM_COUNT];
extern const BtSprite bt_pickup_sprites[4],bt_projectile_sprites[5],bt_glyphs[36],bt_ground;
#endif
