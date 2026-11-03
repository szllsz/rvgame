#ifndef FIDELITY_STRUCTURE_DATA_H
#define FIDELITY_STRUCTURE_DATA_H
#include "fidelity_tables.h"
#include "fidelity_visual_data.h"
enum {BT_ENGINEER=3,BT_PACIFIST=4,BT_WRENCH=8,BT_SCREWDRIVER=9,BT_HAND=10,BT_TASER=11,BT_PRUNER=12,BT_LUMBERJACK_SHIRT=38,BT_STRUCTURE_ITEM_FIRST=28,BT_STRUCTURE_KINDS=6,BT_MINE=5,BT_HEALING_TURRET=4};
typedef struct {BtWeaponDef shot;int16_t burn_damage,burn_scale100,burn_duration,explosion_scale100,healing,animation_speed100,can_bounce,accuracy10000,projectile_radius,projectile_x,projectile_y,projectile_stop;} BtStructureDef;
extern const BtStructureDef bt_structure_defs[BT_STRUCTURE_KINDS];
extern const BtSprite bt_structure_sprites[BT_STRUCTURE_KINDS];
extern const BtSprite bt_structure_projectile_sprites[5];
enum {BT_GARDEN=6};
#endif
