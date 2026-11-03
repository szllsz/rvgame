#ifndef FIDELITY_WORLD_H
#define FIDELITY_WORLD_H
#include "fidelity_run.h"
#include "fidelity_enemy_data.h"
#include "fidelity_wave_data.h"
#include "fidelity_structure_data.h"
enum {TATO_TITLE,TATO_PLAY,TATO_PAUSE,TATO_SHOP,TATO_DEAD,TATO_WIN,TATO_EXIT,TATO_UPGRADE,TATO_CRATE,TATO_FAULT,TATO_COLLECT};
enum {TATO_WIDTH=640,TATO_HEIGHT=480,TATO_SCALE=2,TATO_WORLD_WIDTH=2048,TATO_WORLD_HEIGHT=1536};
enum {TATO_ENEMIES=BT_ENTITY_CAPACITY,TATO_BULLETS=512,TATO_DROPS=50,TATO_EFFECTS=32,BT_MOB_SHOTS=512,
      BT_SPAWN_CAP=512,BT_GROUP_CAP=128,BT_CONSUMABLE_CAP=128,BT_GRID_W=16,BT_GRID_H=12,BT_STRUCTURE_CAP=128};
enum {SOUND_ATTACK=1,SOUND_HIT=2,SOUND_KILL=4,SOUND_HURT=8,SOUND_BUY=16,SOUND_FAIL=32,SOUND_LEVEL=64,SOUND_WAVE=128,SOUND_DEATH=256};
/* Presentation events only; retain the original low bits and gameplay RNG. */
enum {SOUND_TURRET=512,SOUND_FLAME=1024,SOUND_LASER=2048,SOUND_ROCKET=4096,
      SOUND_HEALING=8192,SOUND_EXPLOSION=16384,SOUND_GARDEN=32768};
/* Bits 16..19 are reserved for DRAW status in the telemetry word. */
enum {SOUND_UI_FOCUS=1048576,SOUND_UI_PRESS=2097152};
enum {SOUND_MATERIAL_ATTRACT=4194304,SOUND_MATERIAL_PICKUP=8388608};
/* inventory_slot reuses the existing UI word: offer detail or modal action.
 * Owned weapon index stays in selected (6..11); opening defaults to cancel. */
enum {BT_POPUP_NONE,BT_POPUP_ITEM,BT_POPUP_COMBINE,BT_POPUP_RECYCLE,BT_POPUP_CANCEL};
enum {BT_SETUP_HOME=5};
typedef struct {
    int64_t cooldown;
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    int32_t hp;
    int32_t max_hp;
    int32_t target_x;
    int32_t target_y;
    int32_t actual_range;
    int32_t anim_age;
    int32_t shots;
    int32_t charge_ticks;
    int32_t charge_x;
    int32_t charge_y;
    int32_t secondary_cd;
    int32_t special_cd;
    int32_t bonus_speed;
    int32_t gold_eaten;
    int32_t damage;
    int32_t speed;
    int32_t armor;
    int32_t base_speed;
    int32_t initial_cd;
    int32_t orbit_angle;
    int32_t contact_slow;
    uint32_t nearby[BT_HIT_WORDS];
    int32_t burn_damage;
    int16_t burn_ticks;
    int16_t burn_remaining;
    int16_t move_ticks;
    int16_t boost_count;
    int16_t buff_ticks;
    int16_t spawns;
    int16_t age;
    int8_t kind;
    int8_t type;
    int8_t birth;
    int8_t move;
    int8_t attack;
    int8_t animation;
    int8_t phase;
    int8_t evolution;
    int8_t flash;
    int8_t loot_allowed;
    int8_t orbit_direction;
} TatoEntity;
typedef struct {int32_t x,y,vx,vy,age,damage,dir_x,dir_y;int16_t kind,owner,active,rotate;} BtMobShot;
typedef struct {
    int32_t x;
    int32_t y;
    int32_t value;
    int32_t target_x;
    int32_t target_y;
    int32_t speed;
    int8_t active;
    int8_t attracted;
    int8_t kind;
    int8_t push;
    int8_t idle;
} BtDrop;
typedef struct {int32_t x,y,life,kind;} BtEffect;
typedef struct {int32_t x,y;int16_t kind,type,source,category;} BtSpawn;
typedef struct {
    int16_t definition;
    int16_t next;
    int16_t remaining;
    int16_t interval;
    int16_t first;
    int16_t synthetic_enemy;
    int16_t min_n;
    int16_t max_n;
    int16_t area;
    int16_t border;
} BtActiveGroup;
typedef struct {
    int32_t x,y,cooldown,age;
    uint32_t contacts[BT_HIT_WORDS+1];
    int16_t kind,state,birth,shots;
} BtStructure;
typedef struct {
    BtRun run;
    uint32_t mode,wave,remaining,ticks,frame,selected,message,audio_events,test_invincible,previous;
    uint32_t setup_field,setup_role,setup_weapon,setup_difficulty,setup_endless,inventory_slot;
    uint32_t upgrade_rerolls;
    uint32_t collect_ticks,shop_column;
    uint32_t showcase,showcase_born,showcase_projectiles;
    uint32_t population_removals_at_wave_start,pacifist_bonus_total;
    uint32_t logic_total,normal_ticks,god_ticks,fault,enemy_peak,bullet_peak,mob_peak,spawn_peak,birth_peak,
             drop_merges,entity_overflows,bullet_overflows,mob_overflows,spawn_overflows,group_overflows,
             original_population_removals,consumable_overflows,missed_logic_ticks,kills,damage_taken,damage_dealt,materials_collected;
    int32_t x,y,last_dx,last_dy,invulnerable,regen_ticks,lifesteal_ticks;
    uint8_t upgrade_choices[4],upgrade_tiers[4];
    uint8_t crate_queue[BT_CONSUMABLE_CAP];uint32_t crate_head,crate_count;
    BtWeaponStats weapon_stats[6];
    int32_t weapon_cooldown[6],weapon_motion[6],weapon_age[6],weapon_dx[6],weapon_dy[6];
    int32_t weapon_recoil[6],weapon_forward[6],weapon_x[6],weapon_y[6];
    uint32_t weapon_shots[6],melee_ignored[6][BT_HIT_WORDS];
    /* Original wrench sweep keeps its launch distance/side and hit impulse,
     * while the weapon root continues tracking that target. */
    struct {int32_t distance,target,side,hit_dx,hit_dy;} weapon_sweep[6];
    TatoEntity enemies[TATO_ENEMIES];
    BtProjectile bullets[TATO_BULLETS];
    uint32_t taser_contacts[TATO_BULLETS][BT_HIT_WORDS];
    BtMobShot mob_shots[BT_MOB_SHOTS];
    BtDrop drops[TATO_DROPS],consumables[BT_CONSUMABLE_CAP];
    BtEffect effects[TATO_EFFECTS];
    BtSpawn spawn_queue[BT_SPAWN_CAP];uint32_t spawn_count;
    BtActiveGroup groups[BT_GROUP_CAP];uint32_t group_count;
    int32_t max_enemies,bosses[2],boss_count,event_wave[3],event_type[3],event_elite[3],event_base,event_count,horde,
            crates,legendary_crates,crate_item,items_spawned_this_wave,all_final_bosses_killed,early_finish;
    int16_t grid[BT_GRID_W*BT_GRID_H],grid_next[TATO_ENEMIES],large_enemies[TATO_ENEMIES];
    uint32_t large_count;
    BtStructure structures[BT_STRUCTURE_CAP];
    uint8_t structure_projectile_age[TATO_BULLETS];
    uint32_t structure_peak,structure_overflows,structure_original_removals,structure_shots,mine_explosions,burn_hits,structure_healing;
} TatoGame;
void tato_init(TatoGame *game);
void tato_tick(TatoGame *game,uint32_t buttons,uint32_t pressed);
uint32_t tato_wave_seconds(uint32_t wave);
int tato_price(const TatoGame *game,uint32_t choice);
const char *tato_offer_name(uint32_t offer);
void bt_begin_wave(TatoGame *game);
void bt_waves_init(TatoGame *game);
void bt_waves_tick(TatoGame *game);
void bt_request_spawn(TatoGame *game,int32_t kind,int32_t type,int32_t x,int32_t y,int32_t source,int32_t category);
void bt_enemy_activate(TatoGame *game,uint32_t index);
void bt_enemies_tick(TatoGame *game);
void bt_enemy_bodies(TatoGame *game);
void bt_player_tick(TatoGame *game,uint32_t buttons);
void bt_player_weapons_init(TatoGame *game);
int32_t bt_player_weapon_reach(const TatoGame *game,uint32_t slot);
void bt_player_sweep_pose(const TatoGame *game,uint32_t slot,int32_t *x,int32_t *y,int32_t *angle);
void bt_player_weapon_pose(const TatoGame *game,uint32_t slot,int32_t *x,int32_t *y,int32_t *dx,int32_t *dy);
void bt_loot_tick(TatoGame *game);
void bt_spawn_gold(TatoGame *game,int32_t value1000,int32_t x,int32_t y);
void bt_spawn_garden_fruit(TatoGame *game,int32_t x,int32_t y);
void bt_player_heal(TatoGame *game,int32_t amount);
void bt_queue_crate(TatoGame *game,int32_t kind);
void bt_enemy_die(TatoGame *game,uint32_t index,int32_t killed_by_player);
void bt_enemy_loot(TatoGame *game,uint32_t index);
void bt_enemy_take_hit(TatoGame *game,uint32_t index,int32_t damage,int32_t dx,int32_t dy,int32_t knockback,int32_t critical);
void bt_enemy_decaying_speed(TatoEntity *enemy,int32_t amount);
void bt_taser_slow_tick(TatoGame *game,BtProjectile *projectile);
void bt_enemy_hit_effects(TatoGame *game,uint32_t index,int32_t previous_hp,int32_t dx,int32_t dy,int32_t knockback,int32_t ranged);
void bt_mob_projectiles_tick(TatoGame *game);
void bt_player_hit(TatoGame *game,int32_t damage);
void bt_effect(TatoGame *game,int32_t x,int32_t y,int32_t kind);
uint32_t bt_draw(TatoGame *game,uint32_t count);
int32_t bt_rand_range(TatoGame *game,int32_t low,int32_t high);
int bt_chance(TatoGame *game,int32_t millionths);
void bt_zero(void *memory,uint32_t count);
void bt_world_fault(TatoGame *game,uint32_t bit);
void bt_wave_end(TatoGame *game);
void bt_showcase_start(TatoGame *game);
int bt_showcase_shift(TatoGame *game,unsigned up);
void bt_showcase_restart(TatoGame *game);
void bt_showcase_wave_init(TatoGame *game);
void bt_showcase_replenish(TatoGame *game);
uint32_t bt_showcase_population(uint32_t wave);
#define BT_SHOWCASE_SEED 20261028u
#define BT_SHOWCASE_MAGIC 0x42545339u
int32_t bt_pacifist_bonus(const TatoGame *game);
void bt_endless_last_boss_reinforcements(TatoGame *game);
void bt_structures_init(TatoGame *game);
void bt_structures_schedule(TatoGame *game);
void bt_structures_spawn_tick(TatoGame *game);
void bt_structures_tick(TatoGame *game);
void bt_structure_stats(const TatoGame *game,uint32_t kind,BtWeaponStats *stats);
void bt_structure_bullet_tick(TatoGame *game,BtProjectile *projectile);
void bt_projectile_bounce(TatoGame *game,BtProjectile *projectile,uint32_t hit,int32_t speed);
int32_t bt_structure_damage(const TatoGame *game,uint32_t kind);
#endif
