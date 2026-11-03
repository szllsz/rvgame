#ifndef FIDELITY_GEOMETRY_H
#define FIDELITY_GEOMETRY_H
#include "fidelity_enemy_data.h"
int32_t bt_sine(int32_t angle);
void bt_rotate(int32_t x,int32_t y,int32_t angle,int32_t *rx,int32_t *ry);
int32_t bt_radians_to_angle(int32_t radians1000);
int bt_shape_circle(const BtShapeDef *shape,int32_t x,int32_t y,int32_t radius);
int32_t bt_distance(int32_t dx,int32_t dy);
#endif
