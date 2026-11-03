/* Generated from the user's local original resources. See table_sources.json. */
#ifndef FIDELITY_TABLES_H
#define FIDELITY_TABLES_H
#include <stdint.h>
enum {BT_BOUNCE,BT_CONSUMABLE_HEAL,BT_KNOCKBACK,BT_PICKUP_RANGE,BT_PIERCING,BT_PIERCING_DAMAGE,BT_RECYCLING_GAINS,BT_ARMOR,BT_ATTACK_SPEED,BT_CRIT_CHANCE,BT_DODGE,BT_ELEMENTAL_DAMAGE,BT_ENGINEERING,BT_HARVESTING,BT_HP_REGENERATION,BT_LIFESTEAL,BT_LUCK,BT_MAX_HP,BT_MELEE_DAMAGE,BT_PERCENT_DAMAGE,BT_RANGE,BT_RANGED_DAMAGE,BT_SPEED,BT_XP_GAIN,BT_STAT_COUNT};
enum {BT_WEAPON_KINDS=13,BT_WEAPON_TIERS=4,BT_ITEM_COUNT=39,BT_ROLE_COUNT=5,BT_SET_COUNT=6};
typedef struct {
    const char *name;
    int16_t kind,tier,type,set_mask,value,damage,cooldown,range,crit10000,multiplier100,scale_percent;
    int16_t knockback,recoil,recoil_ms,projectile_speed,projectiles,piercing,reduction100,spread1000,reload,reload_mult100;
} BtWeaponDef;
typedef struct {const char *name;int16_t value,tier,max_nb,effects[BT_STAT_COUNT];} BtItemDef;
typedef struct {const char *name;int16_t effects[BT_STAT_COUNT],gain[BT_STAT_COUNT],starting_extra,ban_melee,unarmed_speed;} BtRoleDef;
typedef struct {const char *name;int16_t stat,bonus[5];} BtSetDef;
extern const BtWeaponDef bt_weapon_defs[BT_WEAPON_KINDS][BT_WEAPON_TIERS];
extern const int16_t bt_weapon_engineering_scale[BT_WEAPON_KINDS][BT_WEAPON_TIERS];
extern const BtItemDef bt_item_defs[BT_ITEM_COUNT];
extern const BtRoleDef bt_role_defs[BT_ROLE_COUNT];
extern const BtSetDef bt_set_defs[BT_SET_COUNT];
extern const char *const bt_stat_names[BT_STAT_COUNT];
#endif
