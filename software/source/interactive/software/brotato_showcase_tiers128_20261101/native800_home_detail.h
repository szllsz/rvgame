#ifndef NATIVE800_HOME_DETAIL_H
#define NATIVE800_HOME_DETAIL_H
#include "fidelity_visual_data.h"
typedef struct{const uint8_t *pixels;uint8_t color,solid;}HomeTile800;
extern const HomeTile800 home_detail_tiles[];
extern const uint16_t home_detail_poses[25][475];
extern const BtSprite home_detail_bounce[90],home_detail_logo;
#endif
