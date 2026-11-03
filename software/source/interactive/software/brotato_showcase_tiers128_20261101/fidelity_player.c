#include "fidelity_world.h"
#include "fidelity_rules.h"
#include "fidelity_geometry.h"
#include "fidelity_selection.h"
extern const int32_t bt_attachment[6][6][2],bt_muzzle[BT_WEAPON_KINDS],bt_sprite_origin[BT_WEAPON_KINDS],bt_expo_table[257];
extern const int16_t bt_accuracy[BT_WEAPON_KINDS][4];
static int32_t maximum(int32_t a,int32_t b) {return a>b?a:b;}
static int32_t minimum(int32_t a,int32_t b) {return a<b?a:b;}
static int32_t clamp(int32_t a,int32_t lo,int32_t hi) {return maximum(lo,minimum(hi,a));}
static int32_t expo(int32_t age,int32_t duration)
{
    if(age<=0) return 0;
    if(age>=duration || duration<=0) return 65536;
    int32_t t=(int32_t)((int64_t)age*65536/duration),index=t>>8,fraction=t&255;
    return bt_expo_table[index]+(bt_expo_table[index+1]-bt_expo_table[index])*fraction/256;
}
int32_t bt_player_weapon_reach(const TatoGame *g,uint32_t slot)
{
    const BtOwnedWeapon *w=&g->run.weapons[slot];const BtWeaponDef *d=&bt_weapon_defs[w->kind][w->tier];
    int32_t origin=bt_sprite_origin[w->kind],recoil=d->recoil*256,t=g->weapon_age[slot],duration=g->weapon_recoil[slot];
    if(g->weapon_motion[slot]<=0)return origin;
    if(t<duration)return origin-(int32_t)((int64_t)recoil*expo(t,duration)/65536);
    t-=duration;
    if(d->type)return origin-recoil+(int32_t)((int64_t)recoil*expo(t,duration)/65536);
    duration=g->weapon_forward[slot];
    if(t<duration)return origin-recoil+(int32_t)((int64_t)(g->weapon_stats[slot].range*256+recoil)*expo(t,duration)/65536);
    t-=duration;duration=g->weapon_stats[slot].action_ticks-g->weapon_recoil[slot]-duration;
    return origin+g->weapon_stats[slot].range*256-(int32_t)((int64_t)g->weapon_stats[slot].range*256*expo(t,duration)/65536);
}
void bt_player_sweep_pose(const TatoGame *g,uint32_t slot,int32_t *x,int32_t *y,int32_t *angle)
{
    const BtOwnedWeapon *w=&g->run.weapons[slot];const BtWeaponDef *d=&bt_weapon_defs[w->kind][w->tier];
    int32_t origin=bt_sprite_origin[w->kind],t=g->weapon_age[slot],recoil=g->weapon_recoil[slot];
    int32_t amount=d->recoil*256,speed=g->run.stat100[BT_ATTACK_SPEED];
    if(speed>0)amount=(int32_t)((int64_t)amount*10000/(10000+speed));
    int32_t side=g->weapon_sweep[slot].side,range=g->weapon_sweep[slot].distance;
    int32_t half=g->weapon_forward[slot]/2,a=side*1843;
    *x=origin;*y=0;*angle=0;
    if(g->weapon_motion[slot]<=0)return;
    if(t<recoil){int32_t f=expo(t,recoil);*x=origin-(int32_t)((int64_t)amount*f/65536);
        *y=(int32_t)((int64_t)side*range*f/131072);*angle=(int32_t)((int64_t)a*f/65536);return;}
    t-=recoil;
    if(t<half){*x=origin-amount+(int32_t)((int64_t)(range*3/4+amount)*t/half);
        *y=side*range/2-(int32_t)((int64_t)side*range*t/(2*half));*angle=a-(int32_t)((int64_t)a*t/half);return;}
    t-=half;int32_t second=g->weapon_forward[slot]-half;
    if(t<second){*x=origin+range*3/4-(int32_t)((int64_t)(range*3/4+amount)*t/second);
        *y=-(int32_t)((int64_t)side*range*t/(2*second));*angle=-(int32_t)((int64_t)a*t/second);return;}
    t-=second;int32_t back=g->weapon_stats[slot].action_ticks-recoil-g->weapon_forward[slot],f=expo(t,back);
    *x=origin-amount+(int32_t)((int64_t)amount*f/65536);
    *y=-side*range/2+(int32_t)((int64_t)side*range*f/131072);*angle=-a+(int32_t)((int64_t)a*f/65536);
}
void bt_player_weapon_pose(const TatoGame *g,uint32_t slot,int32_t *x,int32_t *y,int32_t *dx,int32_t *dy)
{
    if(g->run.weapons[slot].kind!=BT_WRENCH){
        bt_unit_vector(g->weapon_dx[slot],g->weapon_dy[slot],bt_player_weapon_reach(g,slot),x,y);
        *x+=g->weapon_x[slot];*y+=g->weapon_y[slot];
        *dx=g->weapon_dx[slot];*dy=g->weapon_dy[slot];return;
    }
    int32_t ux,uy;bt_unit_vector(g->weapon_dx[slot],g->weapon_dy[slot],32767,&ux,&uy);
    int32_t lx=bt_player_weapon_reach(g,slot),ly=0,angle=0;
    if(g->run.weapons[slot].kind==BT_WRENCH)bt_player_sweep_pose(g,slot,&lx,&ly,&angle);
    *x=g->weapon_x[slot]+(int32_t)(((int64_t)lx*ux-(int64_t)ly*uy)/32767);
    *y=g->weapon_y[slot]+(int32_t)(((int64_t)lx*uy+(int64_t)ly*ux)/32767);
    bt_rotate(ux,uy,angle,dx,dy);
}
void bt_player_hit(TatoGame *g,int32_t damage)
{
    if(g->invulnerable>0 || g->test_invincible || g->run.hp<=0) return;
    int32_t dodge=clamp(g->run.stat100[BT_DODGE],0,6000),amount=0;
    if(!bt_chance(g,dodge*100)) amount=bt_player_damage(damage,g->run.stat100[BT_ARMOR]/100);
    amount=minimum(amount,g->run.hp);g->run.hp-=amount;g->damage_taken+=(uint32_t)amount;
    g->invulnerable=bt_round(bt_iframes_seconds(amount,g->run.max_hp)*60*BT_TICK_ONE);
    if(amount) {
        g->audio_events|=SOUND_HURT;
        if(!g->regen_ticks) g->regen_ticks=maximum(1,bt_round(bt_regen_seconds(g->run.stat100[BT_HP_REGENERATION]/100)*60*BT_TICK_ONE));
    }
    if(!g->run.hp) {g->mode=TATO_DEAD;g->audio_events|=SOUND_DEATH;}
}
static int32_t nearest(const TatoGame *g,int32_t x,int32_t y,int32_t range,const uint32_t *ignored)
{
    int32_t chosen=-1;int64_t distance=(int64_t)range*range;
    for(uint32_t i=0;i<TATO_ENEMIES;++i) {
        const TatoEntity *e=&g->enemies[i];if(e->hp<=0 || e->birth || (ignored && (ignored[i>>5]&(1u<<(i&31))))) continue;
        int64_t dx=e->x-x,dy=e->y-y,d=dx*dx+dy*dy;
        if(d<distance) {distance=d;chosen=(int32_t)i;}
    }
    return chosen;
}
static void lifesteal(TatoGame *g,int32_t chance)
{
    if(g->lifesteal_ticks<=0 && chance>0 && (int32_t)bt_draw(g,1000000)<chance*100) {
        bt_player_heal(g,1);g->lifesteal_ticks=6;
    }
}
static void launch_weapon(TatoGame *g,uint32_t slot)
{
    BtOwnedWeapon *owned=&g->run.weapons[slot];const BtWeaponDef *d=&bt_weapon_defs[owned->kind][owned->tier];
    BtWeaponStats *s=&g->weapon_stats[slot];
    ++g->weapon_shots[slot];g->weapon_age[slot]=0;g->weapon_motion[slot]=s->action_ticks;
    g->weapon_cooldown[slot]=bt_attack_delay(s,d,(int32_t)g->run.weapon_count,g->weapon_shots[slot],bt_draw(g,1000001)/1000000.0);
    g->audio_events|=SOUND_ATTACK;
    if(d->type==1) {
        int32_t x=g->weapon_x[slot],y=g->weapon_y[slot];
        int32_t dx=g->weapon_dx[slot],dy=g->weapon_dy[slot],accuracy=10000-bt_accuracy[owned->kind][owned->tier];
        int32_t base=bt_radians_to_angle(bt_rand_range(g,-accuracy*10,accuracy*10)/100);
        for(int32_t j=0;j<d->projectiles;++j) {
            uint32_t index;for(index=0;index<TATO_BULLETS;++index) if(!g->bullets[index].active) break;
            if(index==TATO_BULLETS) {++g->bullet_overflows;bt_world_fault(g,512u);return;}
            int32_t vx,vy,angle=base+bt_radians_to_angle(bt_rand_range(g,-d->spread1000,d->spread1000));
            bt_rotate(dx,dy,angle,&vx,&vy);
            int32_t ox,oy;bt_unit_vector(dx,dy,bt_sprite_origin[owned->kind]+bt_muzzle[owned->kind],&ox,&oy);
            bt_launch(&g->bullets[index],d,s,x+ox,y+oy,vx,vy);
            if(owned->kind==BT_TASER)bt_zero(g->taser_contacts[index],sizeof(g->taser_contacts[index]));
        }
    } else bt_zero(g->melee_ignored[slot],sizeof(g->melee_ignored[slot]));
}
static int32_t melee_project_quotient(int64_t value)
{
    uint64_t n=value<0?-(uint64_t)value:(uint64_t)value;
    if(n<(1ULL<<45)){
        /* 1/32767=2^-15+2^-30+2^-45+... . For n<2^45 this
         * floor estimate is exact or one low; one remainder test completes it. */
        uint32_t q=(uint32_t)((n+(n>>15)+(n>>30))>>15);
        if(n-(uint64_t)q*32767u>=32767u)++q;
        return value<0?-(int32_t)q:(int32_t)q;
    }
    return (int32_t)(value/32767);
}
static void melee_tick(TatoGame *g,uint32_t slot)
{
    const BtOwnedWeapon *owned=&g->run.weapons[slot];const BtWeaponStats *s=&g->weapon_stats[slot];
    int32_t t=g->weapon_age[slot],recoil=g->weapon_recoil[slot],forward=g->weapon_forward[slot];
    if(t<recoil || t>=recoil+forward) return;
    int32_t reach=bt_player_weapon_reach(g,slot),px=g->weapon_x[slot],py=g->weapon_y[slot];
    int32_t ux,uy;bt_unit_vector(g->weapon_dx[slot],g->weapon_dy[slot],32767,&ux,&uy);
    if(owned->kind==BT_WRENCH){bt_player_weapon_pose(g,slot,&px,&py,&ux,&uy);reach=0;}
    const BtShapeDef *melee=&bt_melee_shapes[owned->kind];
    int64_t ar=reach<0?-(int64_t)reach:reach,ax=melee->x<0?-(int64_t)melee->x:melee->x,ay=melee->y<0?-(int64_t)melee->y:melee->y;
    int64_t wide=2*(ar+ax+ay+melee->a+melee->b+2);
    uint32_t norm=(uint32_t)ux*(uint32_t)ux+(uint32_t)uy*(uint32_t)uy;
    int prefilter=melee->kind==2 && melee->a>=0 && melee->b>=0 && wide>0 && wide<2147483647 &&
        ux>=-32767 && ux<=32767 && uy>=-32767 && uy<=32767 && norm>=536838145u;
    int32_t bound_base=prefilter?(int32_t)wide:0;
    int32_t max_radius=(2147483647-bound_base)/4;

    for(uint32_t i=0;i<TATO_ENEMIES;++i) {
        TatoEntity *e=&g->enemies[i];uint32_t bit=1u<<(i&31),word=i>>5;
        if(e->hp<=0 || e->birth || (g->melee_ignored[slot][word]&bit)) continue;
        const BtShapeDef *hurt=&bt_enemy_defs[e->kind].hurt;
        int32_t dx=e->x+hurt->x-px,dy=e->y+hurt->y-py;
        /* Invert the original rotation: norm>=32767^2/2 bounds either world
         * component by twice the sum of local bounds, including truncation. */
        if(prefilter && hurt->a>=0 && hurt->a<=max_radius){
            int32_t bound=bound_base+4*hurt->a;
            if(dx>bound || dx<-bound || dy>bound || dy<-bound)continue;
        }

        int32_t x=melee_project_quotient((int64_t)dx*ux+(int64_t)dy*uy)-reach;
        int32_t y=melee_project_quotient((int64_t)dy*ux-(int64_t)dx*uy);
        if(!bt_shape_circle(&bt_melee_shapes[owned->kind],x,y,hurt->a)) continue;
        g->melee_ignored[slot][word]|=bit;
        int32_t damage=s->damage,crit=bt_chance(g,s->crit10000*100);
        if(crit) damage=bt_critical_damage(damage,s->multiplier100);
        int32_t kx=owned->kind==BT_WRENCH?g->weapon_sweep[slot].hit_dx:ux;
        int32_t ky=owned->kind==BT_WRENCH?g->weapon_sweep[slot].hit_dy:uy;
        bt_enemy_take_hit(g,i,damage,kx,ky,s->knockback,crit);lifesteal(g,s->lifesteal10000);
    }
}
static void bullet_hit(TatoGame *g,BtProjectile *p,uint32_t index)
{
    TatoEntity *e=&g->enemies[index];if(!p->active || e->hp<=0 || e->birth || (p->ignored[index>>5]&(1u<<(index&31)))) return;
    const BtShapeDef *shape=&bt_enemy_defs[e->kind].hurt;
    int32_t hx=p->x-p->vy/50,hy=p->y+p->vx/50;
    if(p->kind==BT_TASER){int32_t ux,uy;bt_unit_vector(p->vx,p->vy,256,&ux,&uy);hx=p->x+ux;hy=p->y+uy;}
    if(!bt_shape_circle(shape,hx-e->x,hy-e->y,44*256)) return;
    int32_t before=e->hp,bounces=p->bounce,knockback=p->knockback;
    if(!bt_projectile_hit(p,index,&e->hp,e->armor,(int32_t)bt_draw(g,10001))) return;
    bt_enemy_hit_effects(g,index,before,p->vx,p->vy,knockback,1);lifesteal(g,g->run.stat100[BT_LIFESTEAL]);
    if(bounces>0) {
        /* Original spawner chooses a random normal enemy, excluding only the
         * current hit when more than one remains; no target uses random angle. */
        bt_projectile_bounce(g,p,index,bt_weapon_defs[p->kind][0].projectile_speed);
    }
}
void bt_taser_slow_tick(TatoGame *g,BtProjectile *p)
{
    if(!p->active || p->kind!=BT_TASER)return;
    uint32_t *previous=g->taser_contacts[p-g->bullets];
    int32_t ux,uy;bt_unit_vector(p->vx,p->vy,32767,&ux,&uy);
    /* Root position is overwritten by shoot_ex; local SlowHitbox + Collision=(1,4). */
    int32_t x=p->x+(int32_t)(((int64_t)ux-4*(int64_t)uy)*256/32767);
    int32_t y=p->y+(int32_t)(((int64_t)uy+4*(int64_t)ux)*256/32767);
    for(uint32_t i=0;i<TATO_ENEMIES;++i){TatoEntity *e=&g->enemies[i];uint32_t bit=1u<<(i&31),word=i>>5;
        int inside=e->hp>0 && !e->birth && bt_shape_circle(&bt_enemy_defs[e->kind].hurt,x-e->x,y-e->y,34176);
        if(inside){if(!(previous[word]&bit))bt_enemy_decaying_speed(e,-200);previous[word]|=bit;}
        else previous[word]&=~bit;
    }
}
static void bullet_tick(TatoGame *g)
{
    for(uint32_t i=0;i<BT_GRID_W*BT_GRID_H;++i) g->grid[i]=-1;
    g->large_count=0;
    for(uint32_t i=0;i<TATO_ENEMIES;++i) {
        const TatoEntity *e=&g->enemies[i];if(e->hp<=0 || e->birth) continue;
        const BtShapeDef *s=&bt_enemy_defs[e->kind].hurt;
        int32_t extent=maximum(s->a,s->b)+maximum(s->x<0?-s->x:s->x,s->y<0?-s->y:s->y);
        if(extent>64*256) {g->large_enemies[g->large_count++]=(int16_t)i;continue;}
        int32_t x=clamp(e->x/(128*256),0,BT_GRID_W-1),y=clamp(e->y/(128*256),0,BT_GRID_H-1),cell=y*BT_GRID_W+x;
        g->grid_next[i]=g->grid[cell];g->grid[cell]=(int16_t)i;
    }
    uint32_t count=0;
    for(uint32_t i=0;i<TATO_BULLETS;++i) {
        BtProjectile *p=&g->bullets[i];if(!p->active) continue;
        if(p->kind<0){++count;bt_structure_bullet_tick(g,p);continue;}
        ++count;bt_projectile_tick(p);
        if(p->x<0 || p->x>TATO_WORLD_WIDTH*256 || p->y<0 || p->y>TATO_WORLD_HEIGHT*256) {p->active=0;continue;}
        if(p->kind==BT_TASER)bt_taser_slow_tick(g,p);
        int32_t left=clamp((p->x-110*256)/(128*256),0,BT_GRID_W-1),right=clamp((p->x+110*256)/(128*256),0,BT_GRID_W-1);
        int32_t top=clamp((p->y-110*256)/(128*256),0,BT_GRID_H-1),bottom=clamp((p->y+110*256)/(128*256),0,BT_GRID_H-1);
        for(int32_t y=top;y<=bottom;++y) for(int32_t x=left;x<=right;++x)
            for(int32_t j=g->grid[y*BT_GRID_W+x];j>=0;j=g->grid_next[j]) bullet_hit(g,p,(uint32_t)j);
        for(uint32_t j=0;j<g->large_count;++j) bullet_hit(g,p,(uint32_t)g->large_enemies[j]);
    }
    if(count>g->bullet_peak) g->bullet_peak=count;
}
void bt_player_weapons_init(TatoGame *g)
{
    bt_zero(g->weapon_sweep,sizeof(g->weapon_sweep));
    bt_zero(g->taser_contacts,sizeof(g->taser_contacts));
    bt_zero(g->weapon_motion,sizeof(g->weapon_motion));bt_zero(g->weapon_shots,sizeof(g->weapon_shots));
    for(uint32_t i=0;i<g->run.weapon_count;++i) {
        const BtOwnedWeapon *w=&g->run.weapons[i];const BtWeaponDef *d=&bt_weapon_defs[w->kind][w->tier];
        int32_t class_speed=(d->set_mask&8)?bt_role_defs[g->run.role].unarmed_speed*100:0;
        bt_prepare_weapon(&g->weapon_stats[i],d,g->run.stat100,class_speed);
        BtWeaponStats *s=&g->weapon_stats[i];int32_t base=minimum(s->cooldown,180);
        g->weapon_cooldown[i]=bt_round(bt_random_cooldown(base,(int32_t)g->run.weapon_count,bt_draw(g,1000001)/1000000.0)*BT_TICK_ONE);
        g->weapon_dx[i]=32767;g->weapon_dy[i]=0;
        double speed=(g->run.stat100[BT_ATTACK_SPEED]+class_speed)/10000.0;
        double recoil=d->recoil_ms/1000.0;if(speed>0)recoil/=1+speed;
        g->weapon_recoil[i]=bt_round(recoil*60*BT_TICK_ONE);
        double back=speed>0?.2/(1+speed*3):.2;
        g->weapon_forward[i]=maximum(1,s->action_ticks-g->weapon_recoil[i]-bt_round(back*60*BT_TICK_ONE));
    }
}
#include "platform_input.h"
void bt_player_tick(TatoGame *g,uint32_t buttons)
{
    if(g->invulnerable>0) g->invulnerable-=BT_TICK_ONE;
    if(g->lifesteal_ticks>0) --g->lifesteal_ticks;
    int32_t x=((buttons&128u)?1:0)-((buttons&64u)?1:0),y=((buttons&32u)?1:0)-((buttons&16u)?1:0),dx=0,dy=0;
    if(!g->showcase && (platform_controller.buttons&0x10000u) && (platform_controller.aux&0x80000000u))
        input_movement(&platform_controller,&x,&y);
    else {x*=256;y*=256;}
    int32_t speed=maximum(0,bt_round(450*(1+g->run.stat100[BT_SPEED]/10000.0)))*256/60;
    bt_unit_vector(x,y,speed,&dx,&dy);
    g->x=clamp(g->x+dx,24*256,(TATO_WORLD_WIDTH-24)*256);g->y=clamp(g->y+dy,36*256,(TATO_WORLD_HEIGHT-12)*256);
    g->last_dx=dx;g->last_dy=dy;
    if(g->run.hp<g->run.max_hp && g->run.stat100[BT_HP_REGENERATION]>0) {
        if(g->regen_ticks<=0) g->regen_ticks=maximum(1,bt_round(bt_regen_seconds(g->run.stat100[BT_HP_REGENERATION]/100)*60*BT_TICK_ONE));
        g->regen_ticks-=BT_TICK_ONE;
        while(g->regen_ticks<=0) {bt_player_heal(g,1);g->regen_ticks+=maximum(1,bt_round(bt_regen_seconds(g->run.stat100[BT_HP_REGENERATION]/100)*60*BT_TICK_ONE));}
    } else g->regen_ticks=0;
    for(uint32_t i=0;i<g->run.weapon_count;++i) {
        const BtOwnedWeapon *w=&g->run.weapons[i];const BtWeaponDef *d=&bt_weapon_defs[w->kind][w->tier];BtWeaponStats *s=&g->weapon_stats[i];
        g->weapon_x[i]=g->x+bt_attachment[g->run.weapon_count-1][i][0];g->weapon_y[i]=g->y+bt_attachment[g->run.weapon_count-1][i][1];
        if(g->weapon_motion[i]>0) {
            if(w->kind==BT_WRENCH){int32_t target=g->weapon_sweep[i].target;
                if(target>=0 && target<TATO_ENEMIES && g->enemies[target].hp>0)
                    bt_unit_vector(g->enemies[target].x-g->weapon_x[i],g->enemies[target].y-g->weapon_y[i],32767,&g->weapon_dx[i],&g->weapon_dy[i]);}
            g->weapon_motion[i]-=BT_TICK_ONE;g->weapon_age[i]+=BT_TICK_ONE;
            if(!d->type) melee_tick(g,i);
            continue;
        }
        g->weapon_cooldown[i]-=BT_TICK_ONE;
        if(g->weapon_cooldown[i]>0) continue;
        int32_t target=nearest(g,g->weapon_x[i],g->weapon_y[i],(s->range+50)*256,0);
        if(target>=0) {
            bt_unit_vector(g->enemies[target].x-g->weapon_x[i],g->enemies[target].y-g->weapon_y[i],32767,&g->weapon_dx[i],&g->weapon_dy[i]);
            if(w->kind==BT_WRENCH){
                int32_t distance=bt_distance(g->enemies[target].x-g->weapon_x[i],g->enemies[target].y-g->weapon_y[i]);
                distance=minimum(s->range*256,maximum(250*256,distance));
                g->weapon_sweep[i].distance=distance;g->weapon_sweep[i].target=target;
                g->weapon_sweep[i].side=g->weapon_dx[i]<0?1:-1;
                g->weapon_sweep[i].hit_dx=g->weapon_dx[i];g->weapon_sweep[i].hit_dy=g->weapon_dy[i];
                int32_t speed=g->run.stat100[BT_ATTACK_SPEED];
                s->action_ticks=bt_round(bt_melee_action_ticks(distance/256,d->recoil_ms,speed)*BT_TICK_ONE);
                double back=speed>0?.2/(1+speed*3/10000.0):.2;
                g->weapon_forward[i]=maximum(2,s->action_ticks-g->weapon_recoil[i]-bt_round(back*60*BT_TICK_ONE));
            }
            launch_weapon(g,i);
        }
    }
    bullet_tick(g);
}
