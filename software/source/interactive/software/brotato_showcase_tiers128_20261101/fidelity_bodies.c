#include <stdint.h>
static const uint8_t contact_sqrt_seed[400]={
0,1,1,1,2,2,2,2,2,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,
5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
9,9,9,9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,11,11,11,11,11,11,11,
11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,
12,12,12,12,12,12,12,12,12,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
13,13,13,13,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
14,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
16,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
17,17,17,17,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,
18,18,18,18,18,18,18,18,18,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,};
static uint32_t contact_sqrt_floor(uint32_t n)
{
    /* s=floor(sqrt(n>>8)) implies 16*s <= sqrt(n) < 16*(s+1). */
    uint32_t root=(uint32_t)contact_sqrt_seed[n>>8]<<4,trial;
    trial=root+8;if(trial*trial<=n)root=trial;
    trial=root+4;if(trial*trial<=n)root=trial;
    trial=root+2;if(trial*trial<=n)root=trial;
    trial=root+1;if(trial*trial<=n)root=trial;
    return root;
}
static int32_t contact_seed_distance(int32_t dx,int32_t dy)
{
    int32_t x=dx/64,y=dy/64;
    return (int32_t)contact_sqrt_floor((uint32_t)(x*x+y*y))*64;
}
#include "fidelity_world.h"
#include "fidelity_geometry.h"
#include "fidelity_contact_tables.h"
_Static_assert(TATO_ENEMIES<=256,"Ordered body index cache must fit uint8_t");
extern const int32_t bt_body_data[32][3];
static int32_t clamp(int32_t v,int32_t lo,int32_t hi) {return v<lo?lo:v>hi?hi:v;}
static int32_t contact_step(int32_t delta,int32_t share,uint32_t divisor,uint32_t reciprocal)
{
    /* Selected circles have radius <=40 world units: each product fits i32.
     * floor(n/d) from mul-high is at most one low; the remainder fixes it exactly. */
    int32_t product=delta*share;uint32_t numerator=(uint32_t)(product<0?-product:product);
    uint32_t quotient=(uint32_t)(((uint64_t)numerator*reciprocal)>>32);
    if(numerator-quotient*divisor>=divisor)++quotient;
    return product<0?-(int32_t)quotient:(int32_t)quotient;
}
void bt_enemy_bodies(TatoGame *g)
{
    /* Scene bodies are all circles. Eight ordered contact passes preserve collision
     * filtering and inverse-mass displacement without quantizing movement directions.
     * This is a discrete port, not a bitwise reimplementation of Godot's solver. */
    /* Body shape/filter/mass cannot change inside these passes. Cache the live
     * set once; preserve ascending entity order and each cell's reverse order. */
    struct Body {int32_t px,py;int16_t radius,x,y,mass,layer,mask;} bodies[TATO_ENEMIES];
    uint8_t active[TATO_ENEMIES];uint32_t count=0;
    for(uint32_t i=0;i<TATO_ENEMIES;++i) {
        const TatoEntity *e=&g->enemies[i];if(e->hp<=0 || e->birth)continue;
        const BtShapeDef *s=&bt_enemy_defs[e->kind].body;if(s->disabled)continue;
        const int32_t *data=bt_body_data[e->kind];struct Body *b=&bodies[i];
        b->px=e->x;b->py=e->y;b->radius=(int16_t)s->a;b->x=(int16_t)s->x;b->y=(int16_t)s->y;
        /* Every selected mass is an exact multiple of 1000; cancel common units. */
        b->mass=(int16_t)(data[0]/1000);
        if(e->charge_ticks>0 || (e->animation && bt_attack_defs[e->attack].kind==1))b->mass*=5;
        b->layer=(int16_t)data[1];b->mask=(int16_t)data[2];active[count++]=(uint8_t)i;
    }
    for(uint32_t pass=0;pass<8;++pass) {
        for(uint32_t i=0;i<BT_GRID_W*BT_GRID_H;++i)g->grid[i]=-1;
        for(uint32_t n=0;n<count;++n) {
            uint32_t i=active[n];const struct Body *e=&bodies[i];
            int x=clamp(e->px>>15,0,BT_GRID_W-1),y=clamp(e->py>>15,0,BT_GRID_H-1),cell=y*BT_GRID_W+x;
            g->grid_next[i]=g->grid[cell];g->grid[cell]=(int16_t)i;
        }
        uint32_t moved=0;
        for(uint32_t n=0;n<count;++n) {
            uint32_t i=active[n];struct Body *a=&bodies[i];const struct Body *sa=&bodies[i];
            int32_t left=clamp((a->px-112*256)>>15,0,BT_GRID_W-1),right=clamp((a->px+112*256)>>15,0,BT_GRID_W-1);
            int32_t top=clamp((a->py-112*256)>>15,0,BT_GRID_H-1),bottom=clamp((a->py+112*256)>>15,0,BT_GRID_H-1);
            for(int32_t y=top;y<=bottom;++y)for(int32_t x=left;x<=right;++x)
                for(int32_t j=g->grid[y*BT_GRID_W+x];j>=0;j=g->grid_next[j]) {
                    /* Each fixed cell chain has descending entity indices. */
                    if((uint32_t)j<=i)break;
                    struct Body *b=&bodies[j];const struct Body *sb=&bodies[j];
                    if(!((sa->layer&sb->mask) || (sb->layer&sa->mask)))continue;
                    int32_t dx=b->px+sb->x-a->px-sa->x,dy=b->py+sb->y-a->py-sa->y,radius=sa->radius+sb->radius;
                    if(dx<=-radius || dx>=radius || dy<=-radius || dy>=radius)continue;
                    if(dx*dx+dy*dy>=radius*radius)continue;
                    int32_t distance=contact_seed_distance(dx,dy);
                    if(!distance) {dx=64;dy=0;distance=64;}
                    int32_t penetration=radius-distance;if(penetration<64)continue;
                    int32_t ma=sa->mass,mb=sb->mass;
                    int32_t share_a=ma==mb?penetration/2:contact_step(penetration,mb,(uint32_t)(ma+mb),bt_contact_mass_reciprocal[ma+mb]),share_b=penetration-share_a;
                    uint32_t reciprocal=bt_contact_distance_reciprocal[(uint32_t)distance>>6];
                    int32_t move_x=contact_step(dx,share_a,(uint32_t)distance,reciprocal),move_y=contact_step(dy,share_a,(uint32_t)distance,reciprocal);
                    int32_t move_bx=move_x,move_by=move_y;
                    if(share_a!=share_b) {
                        move_bx=contact_step(dx,share_b,(uint32_t)distance,reciprocal);
                        move_by=contact_step(dy,share_b,(uint32_t)distance,reciprocal);
                    }
                    a->px=clamp(a->px-move_x,0,TATO_WORLD_WIDTH*256);
                    a->py=clamp(a->py-move_y,0,TATO_WORLD_HEIGHT*256);
                    b->px=clamp(b->px+move_bx,0,TATO_WORLD_WIDTH*256);
                    b->py=clamp(b->py+move_by,0,TATO_WORLD_HEIGHT*256);++moved;
                }
        }
        if(!moved)break;
    }
    for(uint32_t n=0;n<count;++n){uint32_t i=active[n];g->enemies[i].x=bodies[i].px;g->enemies[i].y=bodies[i].py;}
}
