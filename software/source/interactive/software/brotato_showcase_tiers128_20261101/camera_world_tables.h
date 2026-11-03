#ifndef CAMERA_WORLD_TABLES_H
#define CAMERA_WORLD_TABLES_H
#include "camera_visual_data.h"
#include "camera_effects_attack.h"
#include "camera_effects_visibility.h"
#include "camera_effects_red.h"
_Static_assert(sizeof(camera_bt_world_role_sprites)/sizeof(camera_bt_world_role_sprites[0]) == BT_ROLE_COUNT, "Camera needs every role");
#undef bt_enemy_sprites
#define bt_enemy_sprites (camera_active ? camera_bt_enemy_sprites : native800_bt_enemy_sprites)
#undef bt_pickup_sprites
#define bt_pickup_sprites (camera_active ? camera_bt_pickup_sprites : native800_bt_pickup_sprites)
#undef bt_projectile_sprites
#define bt_projectile_sprites (camera_active ? camera_bt_projectile_sprites : native800_bt_projectile_sprites)
#undef bt_structure_sprites
#define bt_structure_sprites (camera_active ? camera_bt_structure_sprites : native800_bt_structure_sprites)
#undef bt_held_sprites
#define bt_held_sprites (camera_active ? camera_bt_held_sprites : native800_bt_held_sprites)
#undef bt_attack_turrets
#define bt_attack_turrets (camera_active ? camera_bt_attack_turrets : native800_bt_attack_turrets)
#undef bt_attack_projectiles
#define bt_attack_projectiles (camera_active ? camera_bt_attack_projectiles : native800_bt_attack_projectiles)
#undef bt_attack_explosions
#define bt_attack_explosions (camera_active ? camera_bt_attack_explosions : native800_bt_attack_explosions)
#undef bt_red_enemy_projectiles
#define bt_red_enemy_projectiles (camera_active ? camera_bt_red_enemy_projectiles : native800_bt_red_enemy_projectiles)
#undef bt_red_slash
#define bt_red_slash (camera_active ? camera_bt_red_slash : native800_bt_red_slash)
#undef bt_garden_sprite
#define bt_garden_sprite (*(camera_active ? &camera_bt_garden_sprite : &native800_bt_garden_sprite))
#undef bt_taser_sprite
#define bt_taser_sprite (*(camera_active ? &camera_bt_taser_sprite : &native800_bt_taser_sprite))
#undef bt_green_material
#define bt_green_material (*(camera_active ? &camera_bt_green_material : &native800_bt_green_material))
#undef WORLD_ROLE_SPRITES
#define WORLD_ROLE_SPRITES (camera_active ? camera_bt_world_role_sprites : native800_bt_world_role_sprites)
#undef bt_wrench_sweep_sprites
#define bt_wrench_sweep_sprites (camera_active ? camera_bt_world_wrench_sprites : native800_bt_world_wrench_sprites)
#define CAMERA_GROUND_PATCHES 162u
#endif
