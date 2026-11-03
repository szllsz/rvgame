#include "fidelity_combat.h"
#include "fidelity_rules.h"
#include "fidelity_structure_data.h"

static int32_t maximum(int32_t a,int32_t b) {return a>b?a:b;}
static int32_t clamp(int32_t n,int32_t lo,int32_t hi) {return n<lo?lo:n>hi?hi:n;}
void bt_prepare_weapon(BtWeaponStats *out,const BtWeaponDef *def,const int32_t stat[BT_STAT_COUNT],int32_t class_speed)
{
    int32_t speed=stat[BT_ATTACK_SPEED]+class_speed;
    out->damage=bt_weapon_damage(def->damage,def->scale_percent,stat[def->kind==BT_TASER?BT_ELEMENTAL_DAMAGE:def->type==1?BT_RANGED_DAMAGE:BT_MELEE_DAMAGE],stat[BT_PERCENT_DAMAGE]);
    if(def->kind>=0 && def->kind<BT_WEAPON_KINDS && bt_weapon_engineering_scale[def->kind][def->tier]) {
        /* Sum both original scaling terms before the single integer truncation. */
        double damage=def->damage+(stat[BT_MELEE_DAMAGE]/100.0)*(def->scale_percent/100.0)
            +(stat[BT_ENGINEERING]/100.0)*(bt_weapon_engineering_scale[def->kind][def->tier]/100.0);
        int32_t scaled=damage<1?1:(int32_t)damage;
        out->damage=bt_weapon_damage(scaled,0,0,stat[BT_PERCENT_DAMAGE]);
    }
    out->cooldown=bt_cooldown(def->cooldown,speed);
    out->range=maximum(25,(int32_t)(def->range+stat[BT_RANGE]/(def->type==1?100.0:200.0)));
    double motion=def->type==1?bt_ranged_action_ticks(def->recoil_ms,speed):bt_melee_action_ticks(out->range,def->recoil_ms,speed);
    out->action_ticks=bt_round(motion*BT_TICK_ONE);
    out->crit10000=def->crit10000+stat[BT_CRIT_CHANCE];
    out->multiplier100=def->multiplier100;
    out->lifesteal10000=stat[BT_LIFESTEAL];
    out->piercing=def->piercing+stat[BT_PIERCING]/100;
    out->bounce=stat[BT_BOUNCE]/100;
    out->reduction100=clamp(def->reduction100-stat[BT_PIERCING_DAMAGE]/100,0,100);
    out->knockback=maximum(0,def->knockback+stat[BT_KNOCKBACK]/100);
}
int32_t bt_attack_delay(const BtWeaponStats *stats,const BtWeaponDef *def,int32_t count,uint32_t shot,double draw)
{
    double cooldown;
    if(def->reload>0 && shot && shot%(uint32_t)def->reload==0)
        cooldown=stats->cooldown*(def->reload_mult100/100.0);
    else cooldown=bt_random_cooldown(stats->cooldown,count,draw);
    return bt_round(cooldown*BT_TICK_ONE);
}
void bt_launch(BtProjectile *out,const BtWeaponDef *def,const BtWeaponStats *stats,int32_t x,int32_t y,int32_t dx,int32_t dy)
{
    out->x=x;out->y=y;
    bt_unit_vector(dx,dy,def->projectile_speed*256/60,&out->vx,&out->vy);
    out->life_ticks=bt_round(((stats->range+100)/(double)def->projectile_speed)*60*BT_TICK_ONE);
    out->damage=stats->damage;
    for(uint32_t i=0;i<BT_HIT_WORDS;++i) out->ignored[i]=0;
    out->piercing=(int16_t)stats->piercing;out->bounce=(int16_t)stats->bounce;
    out->kind=def->kind;out->crit10000=(int16_t)clamp(stats->crit10000,0,10000);out->multiplier100=(int16_t)stats->multiplier100;
    out->reduction100=(int16_t)stats->reduction100;out->knockback=(int16_t)stats->knockback;out->active=1;
}
void bt_projectile_tick(BtProjectile *p)
{
    if(!p->active) return;
    if(p->life_ticks<=0) {p->active=0;return;}
    p->life_ticks-=BT_TICK_ONE;
    p->x+=p->vx;p->y+=p->vy;
}
int bt_projectile_hit(BtProjectile *p,uint32_t entity,int32_t *health,int32_t armor,int32_t draw)
{
    if(!p->active || entity>=BT_ENTITY_CAPACITY || *health<=0) return 0;
    uint32_t word=entity>>5,bit=1u<<(entity&31u);
    if(p->ignored[word]&bit) return 0;
    int32_t damage=p->damage;
    if(p->crit10000 && draw<=p->crit10000) damage=bt_critical_damage(damage,p->multiplier100);
    damage=bt_enemy_damage(damage,armor);
    *health=maximum(0,*health-damage);
    p->ignored[word]|=bit;
    if(p->bounce>0) {
        /* Caller chooses an unignored target and replaces direction after this hit. */
        --p->bounce;p->life_ticks=bt_round((10100.0/3000.0)*60*BT_TICK_ONE);
        p->damage=bt_pierce_damage(p->damage,50);p->knockback=0;
    } else if(p->piercing>0) {
        --p->piercing;p->damage=bt_pierce_damage(p->damage,p->reduction100);
    } else p->active=0;
    return *health==0?2:1;
}
int bt_circle_overlap(int32_t dx,int32_t dy,int32_t radius)
{
    if(radius>=0 && (dx<=-radius || dx>=radius || dy<=-radius || dy>=radius))return 0;
    /* Surviving |delta|<radius<=32767 proves the signed sum fits i32. */
    if(radius>0 && radius<=32767)return dx*dx+dy*dy<radius*radius;
    return (int64_t)dx*dx+(int64_t)dy*dy<(int64_t)radius*radius;
}
