#include "fidelity_world.h"
#include "fidelity_rules.h"
#include "fidelity_geometry.h"
#include "fidelity_projectile_motion.h"
extern const int32_t bt_predator_orbits[9][4],bt_predator_pivot[3];

static int32_t maximum(int32_t a,int32_t b) {return a>b?a:b;}
static int32_t minimum(int32_t a,int32_t b) {return a<b?a:b;}
static int32_t clamp(int32_t a,int32_t lo,int32_t hi) {return maximum(lo,minimum(hi,a));}
static int64_t attack_cd(TatoGame *g,TatoEntity *e,const BtAttackDef *a)
{
    int32_t every=(int32_t)(a->p[BT_A_LONG_COOLDOWN_EVERY_X_SHOOTS]/1000);
    if(every>0 && e->shots>=every) {e->shots=0;return a->p[BT_A_LONG_COOLDOWN];}
    int64_t lo=a->p[BT_A_COOLDOWN]-a->p[BT_A_MAX_CD_RANDOMIZATION];
    if(lo<1000) lo=1000;
    int64_t hi=a->p[BT_A_COOLDOWN]+a->p[BT_A_MAX_CD_RANDOMIZATION];
    return lo+((hi-lo)*(int64_t)bt_draw(g,1000001))/1000000;
}
static void init_movement(TatoGame *g,TatoEntity *e)
{
    const BtMoveDef *m=&bt_move_defs[e->move];
    int32_t range=(m->kind==5?m->p[BT_M_TARGET_RANGE]:m->p[BT_M_RANGE_AROUND_PLAYER])/1000;
    int32_t random=(m->kind==5?m->p[BT_M_TARGET_RANGE_RANDOMIZATION]:m->p[BT_M_RANGE_RANDOMIZATION])/1000;
    e->actual_range=(range+bt_rand_range(g,-random,random))*256;e->target_x=e->target_y=0;
    if(m->kind==3) {
        e->target_x=bt_rand_range(g,-e->actual_range,e->actual_range);
        e->target_y=bt_rand_range(g,-e->actual_range,e->actual_range);
        if(!m->p[BT_M_ALLOW_WITHIN]) bt_unit_vector(e->target_x,e->target_y,e->actual_range,&e->target_x,&e->target_y);
    }
}
void bt_enemy_activate(TatoGame *g,uint32_t index)
{
    if(g->showcase)++g->showcase_born;
    TatoEntity *e=&g->enemies[index];const BtEnemyDef *d=&bt_enemy_defs[e->kind];
    e->hp=bt_enemy_health(d->stats[BT_E_HEALTH]/1000,d->stats[BT_E_HEALTH_INCREASE_EACH_WAVE]/10,(int32_t)g->wave,(int32_t)g->run.difficulty);
    e->damage=bt_enemy_attack(d->stats[BT_E_DAMAGE]/1000,d->stats[BT_E_DAMAGE_INCREASE_EACH_WAVE]/10,(int32_t)g->wave,(int32_t)g->run.difficulty);
    if(d->boss) {
        if(g->wave<=8) e->hp=bt_round(e->hp*.5);
        else if(g->wave<=12 || (g->wave==20 && g->run.difficulty==5)) e->hp=bt_round(e->hp*.75);
        if(g->wave<=12) e->damage=bt_round(e->damage*.75);
    }
    e->max_hp=e->hp;e->armor=bt_round((d->stats[BT_E_ARMOR]+(int64_t)d->stats[BT_E_ARMOR_INCREASE_EACH_WAVE]*(g->wave-1))/1000.0);
    double endless=bt_endless_factor((int32_t)g->wave)/13.33;
    if(endless>1.75) endless=1.75;
    int32_t random=d->stats[BT_E_SPEED_RANDOMIZATION]/1000;
    e->speed=bt_round((d->stats[BT_E_SPEED]/1000+bt_rand_range(g,-random,random))*(1+endless));
    e->base_speed=e->speed;e->move=d->move;e->attack=d->attack;e->phase=-1;e->birth=0;e->loot_allowed=1;
    e->cooldown=attack_cd(g,e,&bt_attack_defs[e->attack]);e->initial_cd=(int32_t)bt_attack_defs[e->attack].p[BT_A_INITIAL_COOLDOWN];
    /* Buffer's scene starts BoostTimer at .25 s; subsequent callbacks set 4 s. */
    e->special_cd=d->special==BT_SPECIAL_BUFFER?15000:d->special_p[BT_S_BOOST_COOLDOWN]*60;e->orbit_direction=bt_draw(g,2)?1:-1;
    init_movement(g,e);
    if(d->special==BT_SPECIAL_PREDATOR) for(uint32_t j=0;j<9;++j) {
        uint32_t slot;for(slot=0;slot<BT_MOB_SHOTS;++slot) if(!g->mob_shots[slot].active) break;
        if(slot==BT_MOB_SHOTS) {++g->mob_overflows;bt_world_fault(g,128u);return;}
        BtMobShot *b=&g->mob_shots[slot];bt_zero(b,sizeof(*b));b->active=2;b->owner=(int32_t)index;b->kind=1;
        b->dir_x=bt_predator_orbits[j][0];b->dir_y=bt_predator_orbits[j][1];
        b->damage=bt_enemy_attack(bt_predator_orbits[j][2]/1000,bt_predator_orbits[j][3]/10,(int32_t)g->wave,(int32_t)g->run.difficulty);
        if(g->wave<=12) b->damage=bt_round(b->damage*.75);
        b->x=e->x+bt_predator_pivot[0];b->y=e->y+bt_predator_pivot[1];
    }
}
static void movement_target(TatoGame *g,TatoEntity *e,int32_t *x,int32_t *y)
{
    const BtMoveDef *m=&bt_move_defs[e->move];
    *x=e->x;*y=e->y;
    if(m->kind==0 || e->type==2) return;
    if(m->kind==1) {
        if(m->p[BT_M_STOP_CLOSE_TO_TARGET] && bt_circle_overlap(g->x-e->x,g->y-e->y,m->p[BT_M_DISTANCE_TO_TARGET]*256/1000)) return;
        *x=g->x;*y=g->y;return;
    }
    if(m->kind==3) {*x=g->x+e->target_x;*y=g->y+e->target_y;return;}
    if(m->kind==5) {
        int32_t dx,dy;bt_unit_vector(e->x-g->x,e->y-g->y,e->actual_range,&dx,&dy);
        *x=g->x+dx;*y=g->y+dy;return;
    }
    ++e->move_ticks;
    int32_t update=m->p[BT_M_UPDATE_EVERY_X_FRAMES]/1000;
    if((e->target_x==0 && e->target_y==0) || (m->kind==4 && update>=0 && e->move_ticks>=update) ||
       (e->x-e->target_x<10*256 && e->x-e->target_x>-10*256 && e->y-e->target_y<10*256 && e->y-e->target_y>-10*256)) {
        e->move_ticks=0;
        if(m->kind==4) {
            e->target_x=clamp(g->x+bt_rand_range(g,-e->actual_range,e->actual_range),0,TATO_WORLD_WIDTH*256);
            e->target_y=clamp(g->y+bt_rand_range(g,-e->actual_range,e->actual_range),0,TATO_WORLD_HEIGHT*256);
        } else {
            e->target_x=bt_rand_range(g,0,TATO_WORLD_WIDTH*256);e->target_y=bt_rand_range(g,0,TATO_WORLD_HEIGHT*256);
            if(m->kind==6) {
                uint32_t active[TATO_DROPS],count=0;
                for(uint32_t j=0;j<TATO_DROPS;++j) if(g->drops[j].active) active[count++]=j;
                if(count) {BtDrop *drop=&g->drops[active[bt_draw(g,count)]];e->target_x=drop->x;e->target_y=drop->y;}
            }
        }
    }
    *x=e->target_x;*y=e->target_y;
}
static void capture_charge(TatoGame *g,TatoEntity *e,const BtAttackDef *a)
{
    int32_t x=g->x,y=g->y,target=(int32_t)(a->p[BT_A_TARGET]/1000);
    if(target==1) {
        int32_t range=(int32_t)a->p[BT_A_RAND_TARGET_SIZE];
        if(range<0) range=minimum(600000,(int32_t)(a->p[BT_A_MAX_RANGE]/5));
        range=range*256/1000;
        int32_t rx=bt_rand_range(g,-range,range),ry=bt_rand_range(g,-range,range),dx,dy;
        if(a->p[BT_A_ONLY_POSITIONS_IN_TARGET_DIRECTION] && (g->last_dx || g->last_dy)) {
            bt_unit_vector(g->last_dx,g->last_dy,bt_rand_range(g,range/4,range),&dx,&dy);
            bt_rotate(dx,dy,bt_rand_range(g,-1024,1024),&rx,&ry);
        }
        bt_unit_vector(x-e->x,y-e->y,range,&dx,&dy);x+=rx+dx;y+=ry+dy;
    } else if(target==2) {x=bt_rand_range(g,0,TATO_WORLD_WIDTH*256);y=bt_rand_range(g,0,TATO_WORLD_HEIGHT*256);}
    e->charge_x=x-e->x;e->charge_y=y-e->y;
}
static void mob_shot(TatoGame *g,uint32_t owner,const BtAttackDef *a,int32_t x,int32_t y,int32_t dx,int32_t dy,int32_t speed,int32_t rotation)
{
    uint32_t index;
    for(index=0;index<BT_MOB_SHOTS;++index) if(!g->mob_shots[index].active) break;
    if(index==BT_MOB_SHOTS) {++g->mob_overflows;bt_world_fault(g,128u);return;}
    BtMobShot *b=&g->mob_shots[index];bt_zero(b,sizeof(*b));b->active=1;b->x=x;b->y=y;
    if(g->showcase)++g->showcase_projectiles;
    b->owner=(int32_t)owner;b->kind=a->projectile;b->rotate=rotation;
    bt_unit_vector(dx,dy,32767,&b->dir_x,&b->dir_y);bt_unit_vector(dx,dy,speed*256/60,&b->vx,&b->vy);
    b->damage=bt_enemy_attack((int32_t)(a->p[BT_A_DAMAGE]/1000),(int32_t)(a->p[BT_A_DAMAGE_INCREASE_EACH_WAVE]/10),(int32_t)g->wave,(int32_t)g->run.difficulty);
    const TatoEntity *e=&g->enemies[owner];const BtEnemyDef *d=&bt_enemy_defs[e->kind];
    int32_t boost=e->buff_ticks;
    if(d->boss && g->wave<=12) boost-=25;
    if(boost) b->damage=bt_round(b->damage*(1+boost/100.0));
}
static void shoot_projectiles(TatoGame *g,uint32_t owner,const BtAttackDef *a)
{
    TatoEntity *e=&g->enemies[owner];
    int32_t target_x=g->x,target_y=g->y;
    if(a->p[BT_A_SHOOT_IN_UNIT_DIRECTION]) {target_x=e->x+e->charge_x;target_y=e->y+e->charge_y;}
    int32_t base=bt_rand_range(g,-(int32_t)a->p[BT_A_BASE_DIRECTION_RANDOMIZATION],(int32_t)a->p[BT_A_BASE_DIRECTION_RANDOMIZATION]);
    if(a->p[BT_A_BASE_DIRECTION_CONSTANT_SPREAD]) base=(int32_t)a->p[BT_A_BASE_DIRECTION_RANDOMIZATION]*
        (a->p[BT_A_ALTERNATE_BETWEEN_BASE_DIRECTION_SPREAD]?(e->shots&1?1:-1):(bt_draw(g,2)?1:-1));
    base=bt_radians_to_angle(base);
    int32_t base_pos=bt_radians_to_angle(bt_rand_range(g,0,(int32_t)a->p[BT_A_CONSTANT_SPREAD_RAND_BASE_POS]));
    int32_t rotation=bt_radians_to_angle(bt_rand_range(g,-(int32_t)a->p[BT_A_RANDOM_ROTATION],(int32_t)a->p[BT_A_RANDOM_ROTATION]));
    int32_t number=(int32_t)(a->p[BT_A_NUMBER_PROJECTILES]/1000);
    for(int32_t i=0;i<number;++i) {
        int32_t x=a->p[BT_A_SPAWN_PROJECTILES_ON_TARGET]?target_x:e->x;
        int32_t y=a->p[BT_A_SPAWN_PROJECTILES_ON_TARGET]?target_y:e->y;
        int32_t half=(int32_t)(a->p[BT_A_PROJECTILE_SPAWN_SPREAD]*128/1000);
        if(a->p[BT_A_PROJECTILE_SPAWN_ONLY_ON_BORDERS]) {
            int32_t angle=(int32_t)bt_draw(g,4096),ox,oy;
            if(a->p[BT_A_CONSTANT_SPREAD]) angle=base_pos+i*4096/number;
            if(a->angle_count) {
                int32_t ux,uy;bt_unit_vector(target_x-e->x,target_y-e->y,half,&ux,&uy);
                bt_rotate(ux,uy,(int32_t)((int64_t)a->angles[i]*4096/360000),&ox,&oy);
            } else bt_rotate(half,0,angle,&ox,&oy);
            x+=ox;y+=oy;
        } else if(!a->p[BT_A_ATLEAST_ONE_PROJECTILE_ON_TARGET] || i) {x+=bt_rand_range(g,-half,half);y+=bt_rand_range(g,-half,half);}
        int32_t dx=target_x-e->x,dy=target_y-e->y;
        int32_t spread=bt_radians_to_angle((int32_t)a->p[BT_A_PROJECTILE_SPREAD]);
        int32_t angle=base+bt_rand_range(g,-spread,spread);
        if(a->p[BT_A_RANDOM_DIRECTION]) {dx=32767;dy=0;angle=(int32_t)bt_draw(g,4096);}
        if(a->p[BT_A_CONSTANT_SPREAD] && number>1) angle=base-spread+i*spread*2/(number-1);
        if(a->p[BT_A_SHOOT_AWAY_FROM_UNIT]) {dx=x-e->x;dy=y-e->y;angle=rotation;}
        if(a->p[BT_A_SHOOT_TOWARDS_UNIT]) {dx=e->x-x;dy=e->y-y;angle=rotation;}
        if(a->p[BT_A_SHOOT_FROM_PROJ_POS_TOWARDS_PLAYER]) {dx=g->x-x;dy=g->y-y;angle=rotation;}
        int32_t rx,ry;bt_rotate(dx,dy,angle,&rx,&ry);
        int32_t random=(int32_t)(a->p[BT_A_PROJECTILE_SPEED_RANDOMIZATION]/1000);
        int32_t speed=(int32_t)((a->p[BT_A_PROJECTILE_SPEED]+a->p[BT_A_SPEED_CHANGE_AFTER_EACH_PROJECTILE]*i)/1000)+bt_rand_range(g,-random,random);
        mob_shot(g,owner,a,x,y,rx,ry,speed,(int32_t)(a->p[BT_A_ROTATE_PROJECTILE]!=0));
    }
    ++e->shots;
}
static void fire(TatoGame *g,uint32_t index,const BtAttackDef *a)
{
    TatoEntity *e=&g->enemies[index];const BtEnemyDef *d=&bt_enemy_defs[e->kind];
    if(a->kind==1) {
        if(a->p[BT_A_TARGET_CALCULATION_TIMING]) capture_charge(g,e,a);
        e->charge_ticks=(int32_t)(a->p[BT_A_CHARGE_DURATION]*60);
        if(a->p[BT_A_SCALE_CHARGE_DURATION_WITH_RANGE]) e->charge_ticks+=bt_distance(e->charge_x,e->charge_y)*1000/(250*256);
        ++e->shots;e->cooldown=attack_cd(g,e,a)+a->p[BT_A_CHARGE_DURATION]*60;
    } else if(a->kind==2) shoot_projectiles(g,index,a);
    else if(a->kind==3 && a->spawn>=0) {
        for(int32_t i=0;i<a->p[BT_A_NB_TO_SPAWN]/1000;++i) {
            int32_t x=e->x,y=e->y;
            if(a->p[BT_A_SPAWN_AT_RANDOM_POS]) {x=bt_rand_range(g,64*256,(TATO_WORLD_WIDTH-64)*256);y=bt_rand_range(g,64*256,(TATO_WORLD_HEIGHT-64)*256);}
            else if(a->p[BT_A_SPAWN_IN_RADIUS_AROUND_UNIT]>=0) {
                int32_t half=(int32_t)(a->p[BT_A_SPAWN_IN_RADIUS_AROUND_UNIT]*128/1000);
                x+=bt_rand_range(g,-half,half);y+=bt_rand_range(g,-half,half);
            }
            bt_request_spawn(g,a->spawn,1,clamp(x,0,TATO_WORLD_WIDTH*256),clamp(y,0,TATO_WORLD_HEIGHT*256),(int32_t)index,3);++e->spawns;
        }
        if(d->special==BT_SPECIAL_SLASHER_EGG) bt_enemy_die(g,index,0);
    }
    if(d->special==BT_SPECIAL_MOM && e->phase==0 && d->extra[0]>=0 && a==&bt_attack_defs[e->attack]) fire(g,index,&bt_attack_defs[d->extra[0]]);
}
static void phase_tick(TatoGame *g,TatoEntity *e)
{
    const BtEnemyDef *d=&bt_enemy_defs[e->kind];
    if(!d->state_count) return;
    for(int32_t i=e->phase+1;i<d->state_count;++i) {
        const BtBossStateDef *s=&bt_boss_states[d->state_start+i];
        if((int64_t)e->hp*1000>(int64_t)e->max_hp*s->hp && e->evolution*60000<s->time) continue;
        e->phase=i;e->move=s->move;e->attack=s->attack;e->animation=0;e->charge_ticks=0;
        e->cooldown=attack_cd(g,e,&bt_attack_defs[e->attack]);e->initial_cd=(int32_t)bt_attack_defs[e->attack].p[BT_A_INITIAL_COOLDOWN];
        init_movement(g,e);
        int32_t modifier=0;
        if(d->special==BT_SPECIAL_INVOKER && i==1) modifier=150;
        if(d->special==BT_SPECIAL_GARGOYLE) modifier=i==0?-25:-40;
        if(d->special==BT_SPECIAL_MONK) modifier=i==1?25:-50;
        e->speed=bt_round(e->base_speed*(1+modifier/100.0));
    }
}
static void attack_tick(TatoGame *g,uint32_t index)
{
    TatoEntity *e=&g->enemies[index];const BtAttackDef *a=&bt_attack_defs[e->attack];
    if(a->kind==0) return;
    if(e->initial_cd>0) {e->initial_cd-=1000;return;}
    if(e->cooldown>0) e->cooldown-=1000;
    if(e->animation) {
        int32_t before=e->anim_age;e->anim_age+=(int32_t)a->p[BT_A_ATTACK_ANIM_SPEED];
        if(a->start_time>before && a->start_time<=e->anim_age && !a->p[BT_A_TARGET_CALCULATION_TIMING]) capture_charge(g,e,a);
        if(a->fire_time>before && a->fire_time<=e->anim_age) fire(g,index,a);
        if(e->anim_age>=a->anim_length) {
            e->animation=0;
            if(a->kind!=1) {
                e->cooldown=attack_cd(g,e,a);
                if(a->kind==3 && a->p[BT_A_MAX_NB_OF_SPAWNS]>=0 && e->spawns>=a->p[BT_A_MAX_NB_OF_SPAWNS]/1000) e->cooldown=1000000000000LL;
            }
        }
    } else if(e->cooldown<=0) {
        int32_t distance=bt_distance(g->x-e->x,g->y-e->y);
        if(a->kind!=3 && (distance<a->p[BT_A_MIN_RANGE]*256/1000 || distance>a->p[BT_A_MAX_RANGE]*256/1000)) return;
        e->animation=1;e->anim_age=0;
        if(!a->p[BT_A_TARGET_CALCULATION_TIMING] && a->start_time==0) capture_charge(g,e,a);
        if(a->fire_time==0) fire(g,index,a);
    }
}
static void aura_tick(TatoGame *g,uint32_t index)
{
    TatoEntity *e=&g->enemies[index];const BtEnemyDef *d=&bt_enemy_defs[e->kind];
    if(d->special!=BT_SPECIAL_HEALER && d->special!=BT_SPECIAL_BUFFER) return;
    uint32_t possible[TATO_ENEMIES],count=0;
    for(uint32_t j=0;j<TATO_ENEMIES;++j) {
        TatoEntity *other=&g->enemies[j];uint32_t mask=1u<<(j&31),word=j>>5;
        const BtShapeDef *body=&bt_enemy_defs[other->kind].body;
        /* Original BoostZone mask is 4; selected trees use body layer 1. */
        int inside=other->hp>0 && other->type!=2 && j!=index && bt_shape_circle(&d->effect_shape,other->x+body->x-e->x,other->y+body->y-e->y,body->a);
        if(inside) {
            if(d->special==BT_SPECIAL_HEALER && !(e->nearby[word]&mask))
                other->hp=minimum(other->max_hp,other->hp+(d->special_p[BT_S_HEAL]+(int32_t)(g->wave-1)*d->special_p[BT_S_HEAL_INCREASE_EACH_WAVE])/1000);
            e->nearby[word]|=mask;
            if(!other->buff_ticks && bt_enemy_defs[other->kind].boostable) possible[count++]=j;
        } else e->nearby[word]&=~mask;
    }
    if(d->special==BT_SPECIAL_BUFFER && e->special_cd<=0) {
        int32_t n=d->special_p[BT_S_NB_ENTITIES_BOOSTED_AT_ONCE]/1000;
        while(count && n-->0) {
            uint32_t selected=bt_draw(g,count);TatoEntity *other=&g->enemies[possible[selected]];possible[selected]=possible[--count];
            other->buff_ticks=d->special_p[BT_S_DAMAGE_BOOST]/1000;other->hp=other->max_hp=bt_round(other->max_hp*(1+d->special_p[BT_S_HP_BOOST]/100000.0));
            other->damage=bt_round(other->damage*(1+d->special_p[BT_S_DAMAGE_BOOST]/100000.0));
            other->speed=bt_round(other->speed*(1+d->special_p[BT_S_SPEED_BOOST]/100000.0));
        }
        e->special_cd=d->special_p[BT_S_BOOST_COOLDOWN]*60;
    }
}
static void special_tick(TatoGame *g,uint32_t index)
{
    TatoEntity *e=&g->enemies[index];const BtEnemyDef *d=&bt_enemy_defs[e->kind];
    if(e->special_cd>0) e->special_cd-=1000;
    if(d->special==BT_SPECIAL_PURSUER && e->special_cd<=0 && e->boost_count<d->special_p[BT_S_MAX_BOOSTS]/1000) {
        ++e->boost_count;e->bonus_speed+=d->special_p[BT_S_SPEED_ON_BOOST]/1000;
        e->damage=bt_round(bt_enemy_attack(d->stats[BT_E_DAMAGE]/1000,d->stats[BT_E_DAMAGE_INCREASE_EACH_WAVE]/10,(int32_t)g->wave,(int32_t)g->run.difficulty)*
                          (1+e->boost_count*d->special_p[BT_S_DAMAGE_ON_BOOST]/100000.0));
        e->special_cd=d->special_p[BT_S_BOOST_COOLDOWN]*60;
    }
    if(d->special==BT_SPECIAL_EVIL_MOB) for(uint32_t j=0;j<TATO_DROPS;++j) {
        BtDrop *drop=&g->drops[j];
        if(drop->active && !drop->push && bt_circle_overlap(drop->x-e->x,drop->y-e->y,48*256)) {
            e->gold_eaten+=drop->value;drop->active=0;
            int32_t next=e->gold_eaten>=20?2:e->gold_eaten>=10?1:0;
            if(next>e->evolution) {e->evolution=next;e->bonus_speed=-125*next;e->hp*=1+next/2;e->max_hp*=1+next/2;}
        }
    }
    if(d->extra[0]>=0) {
        if(e->secondary_cd>0) e->secondary_cd=maximum(0,e->secondary_cd-1000);
        if(e->charge_ticks>0 && e->secondary_cd<=0) {
            int32_t definition=d->extra[(e->phase>=0 && d->extra[1]>=0)?1:0];
            const BtAttackDef *a=&bt_attack_defs[definition];shoot_projectiles(g,index,a);
            e->secondary_cd=d->special==BT_SPECIAL_MANTIS?15000:d->special==BT_SPECIAL_CROC?45000:(int32_t)a->p[BT_A_COOLDOWN];
        }
    }
    aura_tick(g,index);
}
void bt_enemy_decaying_speed(TatoEntity *e,int32_t amount)
{
    if((int64_t)e->contact_slow*100<-(int64_t)e->speed*70)return;
    if((int64_t)e->contact_slow*100<-(int64_t)e->speed*45)amount/=20;
    int32_t floor=(int32_t)(-(int64_t)e->speed*9/10);
    e->contact_slow+=maximum(amount,floor);
}
void bt_enemy_hit_effects(TatoGame *g,uint32_t index,int32_t previous_hp,int32_t dx,int32_t dy,int32_t knockback,int32_t ranged)
{
    TatoEntity *e=&g->enemies[index];const BtEnemyDef *d=&bt_enemy_defs[e->kind];
    g->damage_dealt+=(uint32_t)maximum(0,previous_hp-e->hp);e->flash=5;g->audio_events|=SOUND_HIT;
    int32_t ux,uy,px,py;bt_unit_vector(dx,dy,32767,&ux,&uy);bt_unit_vector(e->x-g->x,e->y-g->y,32767,&px,&py);
    int32_t resistance=d->stats[BT_E_KNOCKBACK_RESISTANCE];
    if(d->special==BT_SPECIAL_EVIL_MOB && e->evolution>=1) resistance=(500+250*e->evolution)/1000*1000;
    int32_t amount=knockback*(1000-resistance)*256/600;
    e->vx=(int32_t)((int64_t)(ux+px*4)*amount/(32767*5));e->vy=(int32_t)((int64_t)(uy+py*4)*amount/(32767*5));
    if(e->type==2 && (++e->shots>=8 || g->run.items[BT_LUMBERJACK_SHIRT])) e->hp=0;
    if(d->special==BT_SPECIAL_FLY && ranged && bt_chance(g,d->special_p[BT_S_PROJ_CHANCE]*1000)) shoot_projectiles(g,index,&bt_attack_defs[e->attack]);
    if(e->hp<=0) bt_enemy_die(g,index,1);
}
void bt_enemy_take_hit(TatoGame *g,uint32_t index,int32_t damage,int32_t dx,int32_t dy,int32_t knockback,int32_t critical)
{
    (void)critical;TatoEntity *e=&g->enemies[index];if(e->hp<=0 || e->birth) return;
    int32_t before=e->hp;e->hp=maximum(0,e->hp-bt_enemy_damage(damage,e->armor));
    bt_enemy_hit_effects(g,index,before,dx,dy,knockback,0);
}
void bt_enemy_die(TatoGame *g,uint32_t index,int32_t by_player)
{
    TatoEntity *e=&g->enemies[index];const BtEnemyDef *d=&bt_enemy_defs[e->kind];
    if(e->loot_allowed<0) return;
    e->hp=0;e->birth=0;
    if(by_player) {++g->kills;bt_enemy_loot(g,index);g->audio_events|=SOUND_KILL;bt_effect(g,e->x,e->y,1);}
    if(d->special==BT_SPECIAL_SPAWNER) for(int32_t j=0;j<3;++j) {
        /* Sequence RNG calls explicitly: argument evaluation order differs by target. */
        int32_t x=clamp(e->x+bt_rand_range(g,-100*256,100*256),64*256,(TATO_WORLD_WIDTH-64)*256);
        int32_t y=clamp(e->y+bt_rand_range(g,-100*256,100*256),64*256,(TATO_WORLD_HEIGHT-64)*256);
        bt_request_spawn(g,d->summon,1,x,y,(int32_t)index,3);
    }
    for(uint32_t j=0;j<BT_MOB_SHOTS;++j) if(g->mob_shots[j].active && g->mob_shots[j].owner==(int32_t)index) {
        const BtAttackDef *a=&bt_attack_defs[e->attack];
        if(a->p[BT_A_DELETE_PROJECTILE_ON_DEATH] || g->mob_shots[j].active==2) g->mob_shots[j].active=0;
    }
    if(d->boss && by_player && g->wave==20) {
        int32_t bosses=0;
        for(uint32_t j=0;j<TATO_ENEMIES;++j) if((g->enemies[j].hp>0 || g->enemies[j].birth) && bt_enemy_defs[g->enemies[j].kind].boss) ++bosses;
        if(!bosses) {
            g->all_final_bosses_killed=1;
            if(!g->run.endless) {g->remaining=6;g->early_finish=1;}
            else bt_endless_last_boss_reinforcements(g);
        }
    }
    e->loot_allowed=-1;
}
void bt_enemies_tick(TatoGame *g)
{
    uint32_t count=0;
    for(uint32_t i=0;i<TATO_ENEMIES;++i) {
        TatoEntity *e=&g->enemies[i];if(e->hp<=0 || e->birth) continue;
        ++count;const BtEnemyDef *d=&bt_enemy_defs[e->kind];
        if(d->special==BT_SPECIAL_PREDATOR) {
            int32_t increment=(int32_t)((int64_t)bt_predator_pivot[2]*42723/60);
            if(e->phase>=0) increment=increment*5/4;
            e->orbit_angle=(int32_t)(((uint32_t)e->orbit_angle+(uint32_t)(increment*e->orbit_direction))&0x0fffffffu);
        }
        if(e->flash>0) --e->flash;
        ++e->age;
        if(d->boss && e->age%60==0) {++e->evolution;phase_tick(g,e);}
        attack_tick(g,i);if(e->hp<=0) continue;special_tick(g,i);
        int32_t x,y;movement_target(g,e,&x,&y);
        if(e->contact_slow<0)e->contact_slow=minimum(0,e->contact_slow+5);
        else if(e->contact_slow>0)--e->contact_slow;
        int32_t speed=maximum(0,e->speed+e->bonus_speed+e->contact_slow),dx=0,dy=0;
        const BtAttackDef *a=&bt_attack_defs[e->attack];
        if(e->charge_ticks>0) {
            e->charge_ticks-=1000;bt_unit_vector(e->charge_x,e->charge_y,(speed+(int32_t)(a->p[BT_A_CHARGE_SPEED]/1000))*256/60,&dx,&dy);
        } else if(!(e->animation && a->kind==1)) {
            int32_t distance=bt_distance(x-e->x,y-e->y),step=speed*256/60;
            if(distance>step) bt_unit_vector(x-e->x,y-e->y,step,&dx,&dy);else {dx=x-e->x;dy=y-e->y;}
        }
        e->vx=e->vx*9/10;e->vy=e->vy*9/10;
        e->x=clamp(e->x+dx+e->vx,0,TATO_WORLD_WIDTH*256);e->y=clamp(e->y+dy+e->vy,0,TATO_WORLD_HEIGHT*256);
    }
    bt_enemy_bodies(g);
    for(uint32_t i=0;i<TATO_ENEMIES;++i) {
        TatoEntity *e=&g->enemies[i];if(e->hp<=0 || e->birth)continue;
        const BtEnemyDef *d=&bt_enemy_defs[e->kind];int32_t step=maximum(0,e->speed+e->bonus_speed+e->contact_slow)*256/60;
        if(e->type!=2 && g->invulnerable<=0 && (int64_t)e->vx*e->vx+(int64_t)e->vy*e->vy<=(int64_t)step*step &&
           bt_shape_circle(&d->hit,g->x-e->x,g->y-e->y-12*256,21*256)) {
            bt_player_hit(g,e->damage);
            if(e->contact_slow>=-(e->speed*7/10)) {
                int32_t amount=-maximum(200,e->speed*8/10);
                if(e->contact_slow<-(e->speed*45/100))amount/=20;
                amount=maximum(amount,-e->speed*9/10);e->contact_slow+=amount;
            }
            if(d->special==BT_SPECIAL_PURSUER && d->special_p[BT_S_RESET_NB_TIMES_BOOSTED_ON_HIT]) {e->boost_count=e->bonus_speed=0;}
        }
    }
    if(count>g->enemy_peak) g->enemy_peak=count;
}
void bt_mob_projectiles_tick(TatoGame *g)
{
    uint32_t count=0;
    for(uint32_t i=0;i<BT_MOB_SHOTS;++i) {
        BtMobShot *b=&g->mob_shots[i];if(!b->active) continue;
        ++count;b->age+=1000;
        if(b->kind<0 || b->kind>=BT_MOB_PROJECTILE_COUNT) {bt_world_fault(g,256u);return;}
        const BtMobProjectileDef *p=&bt_mob_projectile_defs[b->kind];
        if(b->active==2) {
            const TatoEntity *owner=&g->enemies[b->owner];
            if(owner->hp<=0) {b->active=0;continue;}
            b->vx=minimum(b->dir_x,b->vx+b->dir_y*256/60);
            int32_t x,y;bt_rotate(b->vx,0,owner->orbit_angle>>16,&x,&y);
            b->x=owner->x+bt_predator_pivot[0]+x;b->y=owner->y+bt_predator_pivot[1]+y;
        } else {b->x+=b->vx;b->y+=b->vy;}
        if((p->stop>=0 && b->age>=p->stop) || (p->destroy_outside &&
           (b->x<0 || b->x>TATO_WORLD_WIDTH*256 || b->y<0 || b->y>TATO_WORLD_HEIGHT*256))) {b->active=0;continue;}
        if(b->age<p->enable || (p->disable>=0 && b->age>=p->disable)) continue;
        BtShapeDef shape=p->shape;shape.disabled=0;
        bt_projectile_shape(b->kind,b->age,&shape);
        int32_t dx=g->x-b->x,dy=g->y-b->y-12*256;
        if(b->rotate && b->active!=2) {
            int32_t x=(int32_t)(((int64_t)dx*b->dir_x+(int64_t)dy*b->dir_y)/32767);
            dy=(int32_t)(((int64_t)dy*b->dir_x-(int64_t)dx*b->dir_y)/32767);dx=x;
        }
        if(g->invulnerable<=0 && bt_shape_circle(&shape,dx,dy,21*256)) {bt_player_hit(g,b->damage);if(b->active!=2)b->active=0;}
    }
    if(count>g->mob_peak) g->mob_peak=count;
}
