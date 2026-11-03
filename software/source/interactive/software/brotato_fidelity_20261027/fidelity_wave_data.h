#ifndef FIDELITY_WAVE_DATA_H
#define FIDELITY_WAVE_DATA_H
#include <stdint.h>
enum {BT_G_SPAWN_TIMING,BT_G_REPEATING,BT_G_REPEATING_INTERVAL,BT_G_REDUCE_REPEATING_INTERVAL,BT_G_MIN_REPEATING_INTERVAL,BT_G_AREA,BT_G_SPAWN_DIST_AWAY_FROM_EDGES,BT_G_SPAWN_EDGE_OF_MAP,BT_G_IS_NEUTRAL,BT_G_IS_BOSS,BT_G_IS_HORDE,BT_G_IS_LOOT,BT_G_PREVENT_IF_ELITE,BT_G_PREVENT_IF_HORDE,BT_G_MIN_DIFFICULTY,BT_G_MAX_DIFFICULTY,BT_G_MIN_WAVE,BT_G_MAX_WAVE,BT_G_COUNT};
enum {BT_GROUP_DEF_COUNT=156,BT_UNIT_DEF_COUNT=166,BT_GROUP_REF_COUNT=145,BT_CONDITIONAL_COUNT=0,BT_ELITE_KIND_COUNT=8};
typedef struct {int32_t enemy,type,min_n,max_n,chance,min_distance;} BtWaveUnitDef;
typedef struct {int32_t p[BT_G_COUNT],chance,unit_start,unit_count;} BtGroupDef;
typedef struct {int32_t duration,max_enemies,group_start,group_count,conditional_start,conditional_count;} BtWaveDef;
extern const BtWaveUnitDef bt_wave_units[BT_UNIT_DEF_COUNT];
extern const BtGroupDef bt_group_defs[BT_GROUP_DEF_COUNT];
extern const BtWaveDef bt_wave_defs[20];
extern const int32_t bt_group_refs[BT_GROUP_REF_COUNT],bt_conditional_refs[BT_CONDITIONAL_COUNT?BT_CONDITIONAL_COUNT:1][2];
extern const int32_t bt_groups_data_in_all_waves[3];
extern const int32_t bt_horde_groups[5];
extern const int32_t bt_loot_alien_groups[1];
extern const int32_t bt_endless_groups[1];
extern const int32_t bt_bosses[2];
extern const int32_t bt_elites[8];
extern const int32_t bt_endless_enemies[9];
extern const int32_t bt_elite_group[1];
#endif
