#ifndef FIDELITY_GROUND_REGIONS_H
#define FIDELITY_GROUND_REGIONS_H
#include <stdint.h>
enum {BT_GROUND_LEFT=48,BT_GROUND_TOP=40,BT_GROUND_RIGHT=592,BT_GROUND_BOTTOM=448,BT_GROUND_OUTER=1,BT_GROUND_BASE=109,BT_GROUND_PATCH_COUNT=162};
typedef struct {uint16_t x,y,width,height;} BtGroundPatch;
extern const BtGroundPatch bt_ground_patches[BT_GROUND_PATCH_COUNT];
#endif
