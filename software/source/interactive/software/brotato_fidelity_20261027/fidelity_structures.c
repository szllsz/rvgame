/* Selected original structure rules; fixed-point geometry shares the game contract. */
#include "fidelity_world.h"
#include "fidelity_rules.h"
#include "fidelity_geometry.h"

enum {STRUCTURE_FREE,STRUCTURE_BIRTH,STRUCTURE_READY,STRUCTURE_RETIRING};
enum {STRUCTURE_QUEUE=4,STRUCTURE_LIMIT=100,EXPLOSION_KIND=100};
static int32_t maximum(int32_t a,int32_t b){return a>b?a:b;}
static int32_t minimum(int32_t a,int32_t b){return a<b?a:b;}
static int32_t clamp(int32_t v,int32_t lo,int32_t hi){return maximum(lo,minimum(hi,v));}

int32_t bt_structure_damage(const TatoGame *g,uint32_t kind)
{
    const BtStructureDef *d=&bt_structure_defs[kind];
    return bt_weapon_damage(d->shot.damage,d->shot.scale_percent,g->run.stat100[BT_ENGINEERING],0);
}
void bt_structure_stats(const TatoGame *g,uint32_t kind,BtWeaponStats *s)
{
    const BtStructureDef *d=&bt_structure_defs[kind];const BtWeaponDef *w=&d->shot;
    bt_zero(s,sizeof(*s));s->damage=bt_structure_damage(g,kind);s->cooldown=maximum(2,w->cooldown);
    s->range=maximum(25,w->range);s->crit10000=w->crit10000;s->multiplier100=w->multiplier100;
    s->piercing=w->piercing+g->run.stat100[BT_PIERCING]/100;
    s->bounce=d->can_bounce?g->run.stat100[BT_BOUNCE]/100:0;
    s->reduction100=clamp(w->reduction100-g->run.stat100[BT_PIERCING_DAMAGE]/100,0,100);
    s->knockback=maximum(0,w->knockback+g->run.stat100[BT_KNOCKBACK]/100);
}
void bt_structures_init(TatoGame *g)
{bt_zero(g->structures,sizeof(g->structures));bt_zero(g->structure_projectile_age,sizeof(g->structure_projectile_age));}
static void position_in_area(TatoGame *g,int32_t bx,int32_t by,int32_t area,int32_t *x,int32_t *y)
{
    int32_t half=area*128,edge=64*256;
    bx=clamp(bx,edge+half,TATO_WORLD_WIDTH*256-edge-half);
    by=clamp(by,edge+half,TATO_WORLD_HEIGHT*256-edge-half);
    *x=bt_rand_range(g,bx-half,bx+half);*y=bt_rand_range(g,by-half,by+half);
}
static void request_structure(TatoGame *g,int32_t kind,int32_t bx,int32_t by,int32_t area)
{
    if(g->spawn_count==BT_SPAWN_CAP){++g->spawn_overflows;bt_world_fault(g,4u);return;}
    int32_t x,y;
    if(g->run.role==BT_ENGINEER && kind!=BT_MINE)position_in_area(g,bx,by,area,&x,&y);
    else {x=bt_rand_range(g,160*256,(TATO_WORLD_WIDTH-160)*256);y=bt_rand_range(g,160*256,(TATO_WORLD_HEIGHT-160)*256);}
    BtSpawn *s=&g->spawn_queue[g->spawn_count++];s->x=x;s->y=y;s->kind=(int16_t)kind;s->type=0;s->source=-1;s->category=STRUCTURE_QUEUE;
    if(g->spawn_count>g->spawn_peak)g->spawn_peak=g->spawn_count;
}
void bt_structures_schedule(TatoGame *g)
{
    if(!g->ticks || g->ticks%60)return;
    uint32_t effects=0;
    for(uint32_t i=0;i<g->run.weapon_count;++i)if(g->run.weapons[i].kind==BT_WRENCH || g->run.weapons[i].kind==BT_SCREWDRIVER || g->run.weapons[i].kind==BT_PRUNER)++effects;
    for(uint32_t k=0;k<BT_STRUCTURE_KINDS;++k)effects+=g->run.items[BT_STRUCTURE_ITEM_FIRST+k];
    if(!effects)return;
    int32_t bx,by;position_in_area(g,TATO_WORLD_WIDTH*128,TATO_WORLD_HEIGHT*128,TATO_WORLD_WIDTH/3,&bx,&by);
    int32_t area=(int32_t)minimum(600,400+(int32_t)effects*10);uint32_t seconds=g->ticks/60;
    for(uint32_t i=0;i<g->run.weapon_count && !g->fault;++i){
        const BtOwnedWeapon *w=&g->run.weapons[i];
        if(w->kind==BT_WRENCH && seconds==1)request_structure(g,w->tier,bx,by,area);
        if(w->kind==BT_PRUNER && seconds==1){request_structure(g,BT_GARDEN,bx,by,area);if(!g->fault)g->spawn_queue[g->spawn_count-1].type=w->tier;}
        if(w->kind==BT_SCREWDRIVER && (seconds==1 || seconds%(12-3*w->tier)==0))request_structure(g,BT_MINE,bx,by,area);
    }
    for(uint32_t kind=0;kind<BT_STRUCTURE_KINDS && !g->fault;++kind){
        if(seconds!=1 && (kind!=BT_MINE || seconds%12))continue;
        for(uint32_t i=0;i<g->run.items[BT_STRUCTURE_ITEM_FIRST+kind] && !g->fault;++i)request_structure(g,(int32_t)kind,bx,by,area);
    }
}
void bt_structures_spawn_tick(TatoGame *g)
{
    int32_t queued=-1;
    for(uint32_t i=g->spawn_count;i>0;--i)if(g->spawn_queue[i-1].category==STRUCTURE_QUEUE){queued=(int32_t)i-1;break;}
    if(queued<0)return;
    /* Match the original pre-birth >100 rule, including mine priority and the
     * five-second non-shooting retirement of other structures. */
    uint16_t live[BT_STRUCTURE_CAP],mines[BT_STRUCTURE_CAP];uint32_t n=0,m=0;
    for(uint32_t i=0;i<BT_STRUCTURE_CAP;++i)if(g->structures[i].state==STRUCTURE_READY){live[n++]=(uint16_t)i;if(g->structures[i].kind==BT_MINE)mines[m++]=(uint16_t)i;}
    while(n>STRUCTURE_LIMIT){
        uint32_t index;
        if(m){uint32_t at=bt_draw(g,m);index=mines[at];mines[at]=mines[--m];}
        else index=live[bt_draw(g,n)];
        BtStructure *s=&g->structures[index];s->state=s->kind==BT_MINE?STRUCTURE_FREE:STRUCTURE_RETIRING;
        if(s->kind==BT_GARDEN)s->contacts[1]=300;else s->age=300;
        for(uint32_t j=0;j<n;++j)if(live[j]==index){live[j]=live[--n];break;}
        ++g->structure_original_removals;
    }
    uint32_t free;for(free=0;free<BT_STRUCTURE_CAP;++free)if(!g->structures[free].state)break;
    if(free==BT_STRUCTURE_CAP){++g->structure_overflows;bt_world_fault(g,1u<<22);return;}
    BtSpawn pending=g->spawn_queue[queued];
    for(uint32_t j=(uint32_t)queued+1;j<g->spawn_count;++j)g->spawn_queue[j-1]=g->spawn_queue[j];
    --g->spawn_count;
    BtStructure *s=&g->structures[free];bt_zero(s,sizeof(*s));s->x=pending.x;s->y=pending.y;s->kind=pending.kind;s->state=STRUCTURE_BIRTH;s->birth=60;s->age=-1;
    if(s->kind==BT_GARDEN)s->contacts[0]=(uint32_t)pending.type;
}
static BtProjectile *free_projectile(TatoGame *g)
{
    for(uint32_t i=0;i<TATO_BULLETS;++i)if(!g->bullets[i].active)return &g->bullets[i];
    ++g->bullet_overflows;bt_world_fault(g,512u);return 0;
}
static void explode(TatoGame *g,uint32_t kind,int32_t x,int32_t y,int32_t damage)
{
    BtProjectile *p=free_projectile(g);if(!p)return;
    bt_zero(p,sizeof(*p));p->active=1;p->kind=(int16_t)(-EXPLOSION_KIND-(int32_t)kind);p->x=x;p->y=y;
    g->structure_projectile_age[p-g->bullets]=0;
    p->damage=damage;p->life_ticks=3*BT_TICK_ONE;p->multiplier100=200;
    bt_effect(g,x,y,2);g->audio_events|=SOUND_ATTACK|SOUND_EXPLOSION;
}
static int mine_overlap(int32_t dx,int32_t dy,int32_t radius)
{
    /* Godot3 Capsule height32 is its straight segment, rotated 90 degrees. */
    dx=dx<0?-dx:dx;dx=maximum(0,dx-16*256);
    return bt_circle_overlap(dx,dy,radius+24*256);
}
static void mine_tick(TatoGame *g,BtStructure *s)
{
    uint32_t current[BT_HIT_WORDS+1]={0},left=0,inside=0;
    for(uint32_t i=0;i<TATO_ENEMIES;++i){const TatoEntity *e=&g->enemies[i];
        if(e->hp<=0 || e->birth || e->type==2)continue;
        const BtShapeDef *b=&bt_enemy_defs[e->kind].body;
        if(mine_overlap(e->x+b->x-s->x,e->y+b->y-s->y-16*256,b->a))current[i>>5]|=1u<<(i&31);
    }
    if(mine_overlap(g->x-s->x,g->y-s->y-28*256,24*256))current[BT_HIT_WORDS]=1;
    for(uint32_t i=0;i<BT_HIT_WORDS+1;++i){left|=s->contacts[i]&~current[i];inside|=current[i];s->contacts[i]=current[i];}
    if(inside)s->shots=1;
    if(left){explode(g,BT_MINE,s->x,s->y,bt_structure_damage(g,BT_MINE));s->state=0;++g->mine_explosions;}
}
static int32_t target(const TatoGame *g,const BtStructure *s,int32_t *dx,int32_t *dy)
{
    int32_t range=bt_structure_defs[s->kind].shot.range*256;int64_t best=(int64_t)range*range+1;int32_t chosen=-1;
    if(s->kind==BT_HEALING_TURRET){
        *dx=g->x-s->x;*dy=g->y-s->y;
        int64_t distance=(int64_t)(*dx)*(*dx)+(int64_t)(*dy)*(*dy);
        return distance>0 && distance<best;
    }
    for(uint32_t i=0;i<TATO_ENEMIES;++i){const TatoEntity *e=&g->enemies[i];if(e->hp<=0 || e->birth)continue;
        int32_t x=e->x-s->x,y=e->y-s->y;int64_t d=(int64_t)x*x+(int64_t)y*y;
        if(d>0 && d<best){best=d;chosen=(int32_t)i;*dx=x;*dy=y;}
    }
    return chosen>=0;
}
static int32_t next_cooldown(TatoGame *g,const BtStructure *s)
{
    static const int16_t garden_cooldown[4]={900,840,720,600};
    int32_t base=s->kind==BT_GARDEN?garden_cooldown[s->contacts[0]]:maximum(2,bt_structure_defs[s->kind].shot.cooldown);
    return bt_round((maximum(10,base*7)/10.0+(base*1.3-maximum(10,base*7)/10.0)*bt_draw(g,1000001)/1000000.0)*BT_TICK_ONE);
}
static void turret_shoot(TatoGame *g,BtStructure *s,int32_t dx,int32_t dy,int found)
{
    const BtStructureDef *d=&bt_structure_defs[s->kind];BtWeaponStats stats;bt_structure_stats(g,(uint32_t)s->kind,&stats);
    if(found){
        int32_t angle=bt_radians_to_angle(bt_rand_range(g,-(10000-d->accuracy10000),10000-d->accuracy10000)/10);
        int32_t ux,uy;bt_unit_vector(dx,dy,32767,&ux,&uy);bt_rotate(ux,uy,angle,&dx,&dy);
        s->contacts[0]=(uint32_t)dx;s->contacts[1]=(uint32_t)dy;
    }else {dx=(int32_t)s->contacts[0];dy=(int32_t)s->contacts[1];if(!dx && !dy)dx=32767;}
    for(int32_t j=0;j<d->shot.projectiles;++j){
        BtProjectile *p=free_projectile(g);if(!p)return;
        /* At the original .2s shoot key: Animation y=-18, scale.y=.8,
         * Muzzle y=-12; Q8 encodes -27.6 world units. */
        bt_launch(p,&d->shot,&stats,s->x,s->y-7066,dx,dy);
        g->structure_projectile_age[p-g->bullets]=0;
        if(d->projectile_stop>0)p->life_ticks=minimum(p->life_ticks,d->projectile_stop*BT_TICK_ONE);
        ++g->structure_shots;
    }
    ++s->shots;g->audio_events|=SOUND_ATTACK|(SOUND_TURRET<<(unsigned)s->kind);
}
static void burn_tick(TatoGame *g,uint32_t i)
{
    TatoEntity *e=&g->enemies[i];if(e->hp<=0 || e->birth || !e->burn_remaining)return;
    if(--e->burn_ticks>0)return;
    e->burn_ticks=30;--e->burn_remaining;
    int32_t damage=minimum(e->hp,e->burn_damage);e->hp-=damage;g->damage_dealt+=(uint32_t)damage;
    e->flash=5;g->audio_events|=SOUND_HIT;++g->burn_hits;
    if(!e->hp)bt_enemy_die(g,i,1);
}
void bt_structures_tick(TatoGame *g)
{
    for(uint32_t i=0;i<TATO_ENEMIES;++i)burn_tick(g,i);
    uint32_t count=0;
    for(uint32_t i=0;i<BT_STRUCTURE_CAP && !g->fault;++i){BtStructure *s=&g->structures[i];if(!s->state)continue;++count;
        if(s->state==STRUCTURE_RETIRING){
            if(s->kind!=BT_GARDEN){if(--s->age<=0)s->state=0;continue;}
            /* Garden overrides should_shoot without the base pending-die guard. */
            if(!s->contacts[1] || --s->contacts[1]==0){s->state=0;continue;}
        }
        if(s->state==STRUCTURE_BIRTH){if(--s->birth<=0){s->state=STRUCTURE_READY;s->age=-1;}continue;}
        if(s->kind==BT_MINE){mine_tick(g,s);continue;}
        if(s->kind==BT_GARDEN){
            s->cooldown-=BT_TICK_ONE;
            if(s->age>=0){int32_t old=s->age;s->age+=s->contacts[0]?BT_TICK_ONE:BT_TICK_ONE/2;
                if(old<12*BT_TICK_ONE && s->age>=12*BT_TICK_ONE){bt_spawn_garden_fruit(g,s->x,s->y);g->audio_events|=SOUND_GARDEN;}
                if(s->age>=18*BT_TICK_ONE){s->age=-1;s->cooldown=next_cooldown(g,s);}
            }else if(s->cooldown<=0)s->age=0;
            continue;
        }
        int32_t dx=0,dy=0,found=target(g,s,&dx,&dy);s->cooldown-=BT_TICK_ONE;
        if(s->age>=0){
            int32_t old=s->age;s->age+=BT_TICK_ONE*bt_structure_defs[s->kind].animation_speed100/100;
            if(old<12*BT_TICK_ONE && s->age>=12*BT_TICK_ONE){
                turret_shoot(g,s,dx,dy,found);
                if(!found){s->age=-1;s->cooldown=next_cooldown(g,s);continue;}
            }
            if(s->age>=18*BT_TICK_ONE){s->age=-1;s->cooldown=next_cooldown(g,s);}
        }else if(s->cooldown<=0 && found)s->age=0;
    }
    if(count>g->structure_peak)g->structure_peak=count;
}
static void ignite(TatoGame *g,TatoEntity *e,const BtStructureDef *d)
{
    if(!d->burn_duration || e->hp<=0)return;
    int32_t damage=bt_weapon_damage(d->burn_damage,d->burn_scale100,g->run.stat100[BT_ENGINEERING],0);
    if(!e->burn_remaining){e->burn_ticks=30;e->burn_damage=damage;}else e->burn_damage=maximum(e->burn_damage,damage);
    e->burn_remaining=(int16_t)maximum(e->burn_remaining,d->burn_duration);
}
void bt_projectile_bounce(TatoGame *g,BtProjectile *p,uint32_t hit,int32_t speed)
{
    uint16_t candidates[TATO_ENEMIES];uint32_t n=0;
    for(uint32_t i=0;i<TATO_ENEMIES;++i)if(g->enemies[i].hp>0 && !g->enemies[i].birth && g->enemies[i].type==1)candidates[n++]=(uint16_t)i;
    int32_t dx=32767,dy=0;
    if(n>1){uint32_t i;do i=candidates[bt_draw(g,n)];while(i==hit);dx=g->enemies[i].x-p->x;dy=g->enemies[i].y-p->y;}
    else bt_rotate(32767,0,(int32_t)bt_draw(g,4096),&dx,&dy);
    bt_unit_vector(dx,dy,speed*256/60,&p->vx,&p->vy);
    p->life_ticks=bt_round(10100.0/speed*60*BT_TICK_ONE);
}
static void projectile_center(const BtProjectile *p,const BtStructureDef *d,int explosion,int32_t *x,int32_t *y)
{
    *x=p->x;*y=p->y;
    if(explosion)*y+=3*256*d->explosion_scale100/100;
    else {
        int32_t ux,uy;bt_unit_vector(p->vx,p->vy,32767,&ux,&uy);
        *x+=(int32_t)(((int64_t)d->projectile_x*ux-(int64_t)d->projectile_y*uy)/32767);
        *y+=(int32_t)(((int64_t)d->projectile_x*uy+(int64_t)d->projectile_y*ux)/32767);
    }
}
void bt_structure_bullet_tick(TatoGame *g,BtProjectile *p)
{
    int explosion=p->kind<=-EXPLOSION_KIND;uint32_t kind=(uint32_t)(explosion?-p->kind-EXPLOSION_KIND:-p->kind-1);
    if(kind>=BT_STRUCTURE_KINDS){bt_world_fault(g,1u<<23);return;}
    const BtStructureDef *d=&bt_structure_defs[kind];uint8_t *age=&g->structure_projectile_age[p-g->bullets];
    if(!explosion && d->projectile_stop && *age>=d->projectile_stop){p->active=0;return;}
    if(*age<255)++*age;
    bt_projectile_tick(p);if(!p->active)return;
    if(!explosion && (p->x<0 || p->x>TATO_WORLD_WIDTH*256 || p->y<0 || p->y>TATO_WORLD_HEIGHT*256)){p->active=0;return;}
    if(d->healing && !explosion){
        if(bt_circle_overlap(p->x-g->x,p->y-g->y+12*256,d->projectile_radius+21*256)){
            int32_t hp=g->run.hp;bt_player_heal(g,p->damage);g->structure_healing+=(uint32_t)(g->run.hp-hp);p->active=0;
        }return;
    }
    int32_t radius=explosion?(int32_t)((int64_t)37719*d->explosion_scale100/100):d->projectile_radius;
    int32_t px,py;projectile_center(p,d,explosion,&px,&py);
    for(uint32_t i=0;i<TATO_ENEMIES && p->active && !g->fault;++i){TatoEntity *e=&g->enemies[i];
        if(e->hp<=0 || e->birth || (p->ignored[i>>5]&(1u<<(i&31))))continue;
        const BtShapeDef *shape=&bt_enemy_defs[e->kind].hurt;
        if(!bt_shape_circle(shape,px-e->x,py-e->y,radius))continue;
        if(explosion){
            p->ignored[i>>5]|=1u<<(i&31);bt_enemy_take_hit(g,i,p->damage,e->x-p->x,e->y-p->y,0,0);
        }else{
            int32_t before=e->hp,bounces=p->bounce,knockback=p->knockback;
            if(d->explosion_scale100){
                explode(g,kind,e->x,e->y,p->damage);int32_t dummy=2147483647;bt_projectile_hit(p,i,&dummy,0,0);
            }else{
                ignite(g,e,d);bt_projectile_hit(p,i,&e->hp,e->armor,(int32_t)bt_draw(g,10001));
                bt_enemy_hit_effects(g,i,before,p->vx,p->vy,knockback,1);
            }
            if(bounces>0){bt_projectile_bounce(g,p,i,d->shot.projectile_speed);projectile_center(p,d,0,&px,&py);}
        }
    }
}
