#ifndef FIDELITY_RUN_H
#define FIDELITY_RUN_H
#include "fidelity_combat.h"
#include "fidelity_growth.h"
enum {BT_UPGRADE_QUEUE=128,BT_OK=0,BT_NO_MONEY=1,BT_FULL=2,BT_SOLD=3,BT_NO_PAIR=4,BT_LIMIT=5,BT_LOCKED_ALL=6};
typedef struct {int16_t kind,tier;} BtOwnedWeapon;
typedef struct {int16_t weapon,id,tier,price_wave,locked,active;} BtOffer;
typedef struct {
    int32_t raw[BT_STAT_COUNT],stat100[BT_STAT_COUNT];
    int32_t hp,max_hp,gold,bonus_gold,level,pending_upgrades,upgrade_head;
    int64_t xp10000;
    uint16_t upgrade_queue[BT_UPGRADE_QUEUE],items[BT_ITEM_COUNT];
    BtOwnedWeapon weapons[6];
    BtOffer offers[4];
    uint32_t random,wave,role,difficulty,endless,weapon_count,rerolls,free_reroll;
    uint32_t fault,upgrade_peak;
    uint8_t previous_upgrades[4],previous_upgrade_count;
} BtRun;
uint32_t bt_random(BtRun *run);
void bt_run_init(BtRun *run,uint32_t role,uint32_t weapon,uint32_t danger,uint32_t endless,uint32_t seed);
void bt_run_refresh(BtRun *run);
void bt_run_add_xp(BtRun *run,int32_t value);
void bt_run_harvest(BtRun *run);
void bt_run_harvest_bonus(BtRun *run,int32_t bonus);
int32_t bt_tier_from_draw(int32_t wave,int32_t luck100,int32_t draw1000000);
void bt_shop_enter(BtRun *run);
int32_t bt_offer_price(const BtRun *run,uint32_t slot);
int32_t bt_run_purchase(BtRun *run,uint32_t slot);
int32_t bt_run_reroll(BtRun *run);
int32_t bt_run_combine(BtRun *run,uint32_t slot);
int32_t bt_run_recycle(BtRun *run,uint32_t slot);
int bt_run_can_combine(const BtRun *run,uint32_t slot);
int32_t bt_run_recycle_value(const BtRun *run,uint32_t slot);
void bt_run_upgrade_offers(BtRun *run,uint8_t choices[4],uint8_t tiers[4]);
void bt_run_apply_upgrade(BtRun *run,uint32_t choice,uint32_t tier);
int32_t bt_run_crate_item(BtRun *run,uint32_t legendary);
void bt_run_add_item(BtRun *run,uint32_t item);
#endif
