/* Icicle Cascade: see touhou_model.h and COPYING.upstream. */
#include "touhou_model.h"
#include "direction_table.h"

int32_t th_mul_q30(int32_t a,int32_t b) {
    int64_t v=(int64_t)a*b;
    /* Symmetric nearest rounding, defined also for negative arguments. */
    return v>=0?(int32_t)((v+536870912)>>30):-(int32_t)((-v+536870912)>>30);
}

int32_t th_sin(uint32_t angle) {
    uint32_t quadrant=angle>>30,t=angle&0x3fffffffu;
    if(quadrant&1u)t=0x40000000u-t;
    uint32_t i=t>>18,f=t&0x3ffffu;
    int32_t value=th_quarter_sine[i];
    if(f)value+=(int32_t)(((int64_t)(th_quarter_sine[i+1]-value)*f+131072)>>18);
    return quadrant>=2?-value:value;
}

int32_t th_cos(uint32_t angle) {return th_sin(angle+0x40000000u);}

void th_init(ThWorld *w) {
    /* No dependency on a host ABI, heap or freestanding memset. */
    uint8_t *p=(uint8_t *)w;
    for(uint32_t i=0;i<sizeof(*w);i++)p[i]=0;
    for(unsigned i=0;i<TH_CAPACITY;i++)w->bullets[i].next=(uint16_t)(i+1);
    w->bullets[TH_CAPACITY-1].next=TH_NONE;
    w->head=w->tail=TH_NONE;w->free_head=0;w->emitter_groups=1;
    w->boss_x=240*TH_Q;w->boss_y=100*TH_Q;
    /* move_from_towards first computes the initial attraction velocity. */
    w->boss_vy=419430; /* nearest Q21 to (120-100)*.01 */
}

static void spawn(ThWorld *w,int32_t speed,uint32_t angle,int side) {
    uint16_t slot=w->free_head;
    if(slot==TH_NONE){w->overflow++;return;}
    ThBullet *b=&w->bullets[slot];w->free_head=b->next;
    b->x=w->boss_x+side*28*TH_Q;b->y=w->boss_y-42*TH_Q;
    b->vx=th_mul_q30(speed*TH_Q,th_cos(angle));
    b->vy=th_mul_q30(speed*TH_Q,th_sin(angle));
    b->tx=b->ty=0;b->id=++w->next_id;
    b->age=0;b->angle=angle;b->kind=1;
    b->graze_cooldown=b->graze_reset=b->graze_count=0;b->next=TH_NONE;
    if(w->tail!=TH_NONE)w->bullets[w->tail].next=slot;else w->head=slot;
    w->tail=slot;w->active++;w->spawned++;
    if(w->active>w->peak)w->peak=w->active;
}

static void release(ThWorld *w,uint16_t previous,uint16_t slot) {
    ThBullet *b=&w->bullets[slot];uint16_t next=b->next;
    if(previous==TH_NONE)w->head=next;else w->bullets[previous].next=next;
    if(w->tail==slot)w->tail=previous;
    b->kind=0;b->next=w->free_head;w->free_head=slot;w->active--;
}

void th_clear(ThWorld *w){
    w->cleared+=w->active;
    while(w->head!=TH_NONE)release(w,TH_NONE,w->head);
}
void th_update_checked(ThWorld *w,ThCollision collision,void *context) {
    w->frame++;
    if(w->ended)return;
    /* process_boss precedes projectile processing and the emitter task. */
    w->boss_y+=w->boss_vy;
    w->boss_vy=th_mul_q30(120*TH_Q-w->boss_y,10737418); /* .01 */
    if(w->frame>TH_ATTACK_DELAY+TH_ATTACK_TICKS){
        th_clear(w);
        w->ended=1;return;
    }
    uint16_t previous=TH_NONE,slot=w->head;
    while(slot!=TH_NONE){
        ThBullet *b=&w->bullets[slot];uint16_t next=b->next;b->age++;
        int32_t old_x=b->x,old_y=b->y;
        b->x+=b->vx;b->y+=b->vy;
        if(b->kind==1){b->vx=th_mul_q30(b->vx,966367642);b->vy=th_mul_q30(b->vy,966367642);}
        else {
            b->vx=b->tx+th_mul_q30(b->vx-b->tx,858993459);
            b->vy=b->ty+th_mul_q30(b->vy-b->ty,858993459);
        }
        /* Source enemy size is unrotated 10x20; max viewport dist is 64.
         * These are strict inequalities, not an invented short lifetime. */
        unsigned event=collision?collision(context,b,old_x,old_y):0;
        if(event==1){release(w,previous,slot);w->hit_removed++;}
        else if(!event&&(b->x< -69*TH_Q||b->x>549*TH_Q||b->y< -74*TH_Q||b->y>634*TH_Q)){
            release(w,previous,slot);w->retired++;
        }else previous=slot;
        slot=next;
    }
}
void th_emit(ThWorld *w){
    if(w->ended)return;
    /* Event.started wakes WAIT(20) immediately before scheduler's same-frame
     * visit. Therefore 20+22 waits emit at started+41, then every 22 ticks.
     * New tasks/projectiles append and are not moved on their birth frame. */
    if(w->frame>=101&&(w->frame-101)%22==0){
        const uint32_t tenth=68356528u,turn=273426110u;
        uint32_t a=(uint32_t)(w->round-1)*turn-tenth;
        uint32_t other=0x80000000u-a;
        /* Explicit coincident-emitter pressure variant. Normal is exactly 1;
         * no change to per-emitter rate/trajectory/sprite/lifetime. */
        for(unsigned group=0;group<w->emitter_groups;group++)
            for(int i=0;i<4;i++){spawn(w,8+3*i,a,1);spawn(w,8+3*i,other,-1);}
        w->round++;
    }
    /* Source emitter was created before its projectile tasks. Turn occurs
     * after movement/OOB at age 80; the new velocity moves on age 81. */
    for(uint16_t slot=w->head;slot!=TH_NONE;slot=w->bullets[slot].next){
        ThBullet *b=&w->bullets[slot];
        if(b->kind==1&&b->age==80){
            uint32_t a=b->angle;
            a+=th_cos(a)>0?0x40000000u:0xc0000000u;
            b->tx=th_mul_q30(5*TH_Q/2,th_cos(a));
            b->ty=th_mul_q30(5*TH_Q/2,th_sin(a));
            b->vx=3*b->tx;b->vy=3*b->ty;
            b->angle=a;b->kind=2;w->turned++;
        }
    }
}
void th_tick_checked(ThWorld *w,ThCollision collision,void *context){th_update_checked(w,collision,context);th_emit(w);}
void th_tick(ThWorld *w){th_tick_checked(w,0,0);}
