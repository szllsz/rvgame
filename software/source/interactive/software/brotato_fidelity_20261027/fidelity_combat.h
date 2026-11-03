#ifndef FIDELITY_COMBAT_H
#define FIDELITY_COMBAT_H
#include "fidelity_tables.h"
enum {BT_ENTITY_CAPACITY=192,BT_HIT_WORDS=6,BT_TICK_ONE=65536};
typedef struct {
    int32_t damage,cooldown,range,action_ticks,crit10000,multiplier100,lifesteal10000;
    int32_t piercing,bounce,reduction100,knockback;
} BtWeaponStats;
typedef struct {
    int32_t x,y,vx,vy,life_ticks,damage;
    uint32_t ignored[BT_HIT_WORDS];
    int16_t piercing,bounce,kind,crit10000,multiplier100,reduction100,knockback,active;
} BtProjectile;
void bt_prepare_weapon(BtWeaponStats *out,const BtWeaponDef *definition,const int32_t stats100[BT_STAT_COUNT],int32_t class_attack_speed100);
int32_t bt_attack_delay(const BtWeaponStats *stats,const BtWeaponDef *definition,int32_t count,uint32_t shot,double random_draw);
void bt_launch(BtProjectile *out,const BtWeaponDef *definition,const BtWeaponStats *stats,int32_t x,int32_t y,int32_t dx,int32_t dy);
void bt_projectile_tick(BtProjectile *projectile);
int bt_projectile_hit(BtProjectile *projectile,uint32_t entity,int32_t *health,int32_t armor,int32_t random10000);
int bt_circle_overlap(int32_t dx,int32_t dy,int32_t radius);
#endif
