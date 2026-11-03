#ifndef FIDELITY_RULES_H
#define FIDELITY_RULES_H
#include <stdint.h>
/* Original rule references and independent oracle are in the fidelity report.
 * Stats use hundredths of a stat point; cooldowns are 60 Hz ticks, not seconds.
 * Double is used when refreshing derived stats, keeping original operation order.
 */
int32_t bt_round(double value);
int32_t bt_weapon_damage(int32_t base,int32_t scaling_percent,int32_t scaling_stat100,int32_t percent_damage100);
int32_t bt_cooldown(int32_t base,int32_t attack_speed100);
double bt_ranged_action_ticks(int32_t recoil_milliseconds,int32_t attack_speed100);
double bt_melee_action_ticks(int32_t range,int32_t recoil_milliseconds,int32_t attack_speed100);
double bt_random_cooldown(int32_t base,int32_t weapons,double draw);
int32_t bt_critical_damage(int32_t damage,int32_t multiplier100);
int32_t bt_player_damage(int32_t damage,int32_t armor);
int32_t bt_enemy_damage(int32_t damage,int32_t armor);
double bt_regen_seconds(int32_t regen);
double bt_iframes_seconds(int32_t damage,int32_t max_hp);
int32_t bt_xp_needed(int32_t next_level);
double bt_endless_factor(int32_t wave);
int32_t bt_shop_price(int32_t wave,int32_t base,int32_t weapon,int32_t weapon_price_pct,int32_t item_price_pct,int32_t affected);
int32_t bt_reroll_price(int32_t wave,int32_t count,int32_t modifier);
int32_t bt_recycle_price(int32_t wave,int32_t base,int32_t weapon,int32_t gain,int32_t item_price_pct);
int32_t bt_pierce_damage(int32_t damage,int32_t reduction100);
int32_t bt_enemy_health(int32_t base,int32_t increment100,int32_t wave,int32_t danger);
int32_t bt_enemy_attack(int32_t base,int32_t increment100,int32_t wave,int32_t danger);
uint32_t bt_sqrt(uint32_t value);
void bt_unit_vector(int32_t dx,int32_t dy,int32_t speed,int32_t *vx,int32_t *vy);
uint32_t bt_shoulder_toggle(uint32_t enabled,uint32_t buttons,uint32_t previous);
#endif
