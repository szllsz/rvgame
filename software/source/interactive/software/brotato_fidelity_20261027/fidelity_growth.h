#ifndef FIDELITY_GROWTH_H
#define FIDELITY_GROWTH_H
#include "fidelity_tables.h"
enum {BT_UPGRADE_COUNT=16};
typedef struct {int16_t stat,value[4];} BtUpgradeDef;
extern const BtUpgradeDef bt_upgrade_defs[BT_UPGRADE_COUNT];
extern const uint32_t bt_item_tags[BT_ITEM_COUNT],bt_role_wanted[BT_ROLE_COUNT];
#endif
