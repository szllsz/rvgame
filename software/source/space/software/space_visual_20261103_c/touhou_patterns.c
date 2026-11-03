/* Taisei1.4.6 boss_nonspell_1.c / cirno.c / common_tasks.c / move.h.
 * Exact Normal emitter counts and waits; visual-only RNG is excluded from
 * this explicitly separate gameplay RNG stream. No coroutine stacks/heap. */
#include "touhou_patterns.h"
static int32_t divide(int64_t n,int32_t d){return n>=0?(int32_t)((n+d/2)/d):-(int32_t)((-n+d/2)/d);}
void th_pattern_init(ThWorld *w,ThPattern *p,ThRng *rng,uint32_t seed){
    th_init(w);th_rng_init(rng,seed);*p=(ThPattern){0};p->id=TH_NON_SPELL;
    p->target_x=240*TH_Q;p->target_y=100*TH_Q;p->attraction=53687091;
    w->boss_vy=0;
}
void th_rain_init(ThWorld *w,ThPattern *p){
    th_init(w);*p=(ThPattern){0};p->id=TH_CRYSTAL_RAIN;
    p->target_x=w->boss_x;p->target_y=w->boss_y;p->attraction=107374182;w->boss_vy=0;
}
static void release(ThWorld *w,uint16_t previous,uint16_t slot){
    ThBullet *b=&w->bullets[slot];uint16_t next=b->next;
    if(previous==TH_NONE)w->head=next;else w->bullets[previous].next=next;
    if(w->tail==slot)w->tail=previous;
    b->kind=0;b->next=w->free_head;w->free_head=slot;w->active--;
}
static int inside(int32_t x,int32_t y,int bottom){return x>=64*TH_Q&&x<=416*TH_Q&&y>=64*TH_Q&&y<=bottom*TH_Q;}
static void wander(ThWorld *w,ThPattern *p,ThRng *rng,int distance){
    p->wander_calls++;int32_t dx=0,dy=0,x,y;int bottom=p->id==TH_CRYSTAL_RAIN?230:200;
    for(unsigned i=0;i<32;i++){
        uint32_t a=th_random_angle(th_rng_next(rng));dx=th_cos(a);dy=th_sin(a);
        x=w->boss_x+th_mul_q30(distance*TH_Q,dx);y=w->boss_y+th_mul_q30(distance*TH_Q,dy);
        if(inside(x,y,bottom)){p->target_x=x;p->target_y=y;return;}
    }
    /* hypot(416-64,200-64)/16 in Q21, nearest. Never substitute a clamp. */
    p->wander_fallbacks++;int32_t step=bottom==230?51010420:49461231;
    dx=th_mul_q30(step,dx);dy=th_mul_q30(step,dy);x=w->boss_x;y=w->boss_y;
    while(inside(x+dx,y+dy,bottom)){x+=dx;y+=dy;}
    p->target_x=x;p->target_y=y;
}
static void spawn(ThWorld *w,int32_t x,int32_t y,int32_t vx,int32_t vy,int32_t tx,int32_t ty,unsigned kind){
    uint16_t slot=w->free_head;if(slot==TH_NONE){w->overflow++;return;}
    ThBullet *b=&w->bullets[slot];w->free_head=b->next;*b=(ThBullet){0};
    b->x=x;b->y=y;b->vx=vx;b->vy=vy;b->tx=tx;b->ty=ty;b->id=++w->next_id;
    b->angle=th_angle(vx,vy);b->kind=(uint8_t)kind;b->next=TH_NONE;
    if(w->tail!=TH_NONE)w->bullets[w->tail].next=slot;else w->head=slot;
    w->tail=slot;w->active++;w->spawned++;if(w->active>w->peak)w->peak=w->active;
}
void th_pattern_update(ThWorld *w,ThPattern *p,ThCollision collision,void *context){
    w->frame++;if(w->ended)return;
    w->boss_x+=p->boss_vx;w->boss_y+=w->boss_vy;
    p->boss_vx=th_mul_q30(p->target_x-w->boss_x,p->attraction);
    w->boss_vy=th_mul_q30(p->target_y-w->boss_y,p->attraction);
    if(w->frame>(p->id==TH_CRYSTAL_RAIN?2460u:1260u)){th_clear(w);w->ended=1;return;}
    uint16_t previous=TH_NONE,slot=w->head;
    while(slot!=TH_NONE){
        ThBullet *b=&w->bullets[slot];uint16_t next=b->next;int32_t old_x=b->x,old_y=b->y;
        b->age++;
        if(b->kind==TH_RAIN_DROP){
            /* tx/ty are Q30 acceleration for rain. Analytic accumulated
             * acceleration retains the fractional remainder without another
             * per-bullet array or Q21 acceleration's quadratic drift. */
            int32_t t=b->age;int64_t n=(int64_t)t*(t-1)/2,previous=(int64_t)(t-1)*(t-2)/2;
            b->x+=divide(n*b->tx,512)-divide(previous*b->tx,512);
            b->y+=TH_Q+divide(n*b->ty,512)-divide(previous*b->ty,512);
            b->vx=divide((int64_t)t*b->tx,512);b->vy=TH_Q+divide((int64_t)t*b->ty,512);
        }else{b->x+=b->vx;b->y+=b->vy;}
        if(b->x!=old_x||b->y!=old_y)b->angle=th_angle(b->x-old_x,b->y-old_y);
        int32_t retention=b->kind==TH_CRYSTAL_RANDOM?1049217527:858993459;
        /* Truncate the decaying residual toward zero. Nearest rounding has
         * a nonzero fixed point (e.g.2*.8 rounds back to2), which creates
         * a persistent drift and advances exact-boundary collision frames. */
        if(b->kind==TH_RAIN_DROP){ /* accelerated above; no retention step */ }
        else if(b->kind==TH_CRYSTAL_RANDOM){
            b->vx=b->tx+th_mul_q30(b->vx-b->tx,retention);
            b->vy=b->ty+th_mul_q30(b->vy-b->ty,retention);
        }else{
            b->vx=b->tx+(int32_t)((int64_t)(b->vx-b->tx)*retention/1073741824);
            b->vy=b->ty+(int32_t)((int64_t)(b->vy-b->ty)*retention/1073741824);
        }
        int32_t half_x=b->kind==TH_CRYSTAL_RANDOM?261*TH_Q:25*TH_Q/2;
        int32_t half_y=b->kind==TH_CRYSTAL_RANDOM?266*TH_Q:25*TH_Q/2;
        if(b->kind==TH_RAIN_DROP){half_x=5*TH_Q;half_y=10*TH_Q;}
        else if(b->kind==TH_RAIN_BIG)half_x=half_y=20*TH_Q;
        unsigned event=collision?collision(context,b,old_x,old_y):0;
        if(event==1){release(w,previous,slot);w->hit_removed++;}
        else if(!event&&(b->x< -half_x||b->x>480*TH_Q+half_x||b->y< -half_y||b->y>560*TH_Q+half_y)){
            release(w,previous,slot);w->retired++;
        }else previous=slot;
        slot=next;
    }
}
static void rain_emit(ThWorld *w,ThPattern *p,ThRng *rng,int32_t player_x,int32_t player_y){
    if(w->frame>=60&&(w->frame-60)%10==0)for(unsigned i=0;i<2;i++){
        uint64_t value=th_rng_next(rng);(void)th_rng_next(rng); /* source RNG_ARRAY2, including unused second */
        int32_t x=(int32_t)(((value>>32)*(480u*(uint64_t)TH_Q)+(UINT64_C(1)<<31))>>32),ax,ay;
        th_normalize(player_x-x,player_y,1073742,&ax,&ay);
        spawn(w,x,0,0,TH_Q,ax,ay,TH_RAIN_DROP);
    }
    if(w->frame>=79){
        unsigned time=(w->frame-79)%500;
        if(time==0)wander(w,p,rng,80);
        else if(time==180||time==300)wander(w,p,rng,40);
    }
    /* New child task is pending until the next scheduler boundary:
     * first wander79 + WAIT10 + WAIT80 =169, then every80 for400 ticks. */
    if(w->frame>=169){
        unsigned time=(w->frame-169)%500;
        if(time<=320&&time%80==0){
            int32_t y=w->boss_y-60*TH_Q;uint32_t aim=th_angle(player_x-w->boss_x,player_y-y);
            for(int i=-1;i<=1;i++){
                uint32_t a=aim+(uint32_t)(i*205069583);
                int32_t tx=th_mul_q30(2*TH_Q,th_cos(a)),ty=th_mul_q30(2*TH_Q,th_sin(a));
                spawn(w,w->boss_x,y,divide((int64_t)tx*33,10),divide((int64_t)ty*33,10),tx,ty,TH_RAIN_BIG);
            }
        }
    }
}
void th_pattern_emit(ThWorld *w,ThPattern *p,ThRng *rng,int32_t player_x,int32_t player_y){
    if(w->ended)return;
    if(p->id==TH_CRYSTAL_RAIN){rain_emit(w,p,rng,player_x,player_y);return;}
    if(w->frame<79)return;
    unsigned time=(w->frame-79)%300;
    if(time<=8&&!(time&1u)){
        unsigned shot=time/2;uint32_t aim=th_angle(player_x-w->boss_x,player_y-w->boss_y);
        /* Quantize the positive rational base speed downward. Rounding13/3
         * upward accumulates a premature circle-boundary hit on the exact
         * vertical Normal ring; this truncation stays within the position
         * contract while retaining the upstream discrete collision frame. */
        int32_t speed=(9+(int32_t)shot)*TH_Q/3;
        for(unsigned i=0;i<10;i++){
            uint32_t a=aim+(uint32_t)(((uint64_t)i<<32)/10);
            int32_t tx=th_mul_q30(speed,th_cos(a)),ty=th_mul_q30(speed,th_sin(a));
            spawn(w,w->boss_x,w->boss_y,divide((int64_t)tx*(10+7*shot),10),divide((int64_t)ty*(10+7*shot),10),tx,ty,TH_BALL_BLUE);
        }
    }
    if(time>=20&&time<=77&&(time-20)%3==0){
        unsigned i=(time-20)/3;int32_t a=th_random_signed_q30(th_rng_next(rng));
        uint32_t angle=(uint32_t)divide((int64_t)a,4)-0x40000000u;
        spawn(w,w->boss_x+(i&1?-30:30)*TH_Q,w->boss_y-42*TH_Q,
            th_mul_q30(5*TH_Q,th_cos(angle)),th_mul_q30(5*TH_Q,th_sin(angle)),0,4*TH_Q,TH_CRYSTAL_RANDOM);
    }
    if(time==80)p->attraction=21474836;
    if(time==100)wander(w,p,rng,60);
    if(time>=130&&time<=255&&(time-130)%25==0){
        unsigned i=(time-130)/25;uint32_t dif=th_random_float_angle(th_rng_next(rng));
        int32_t speed=(12+(int32_t)i)*TH_Q/4;
        for(unsigned shot=0;shot<20;shot++){
            uint32_t a=dif+shot*0x20000000u;int32_t tx=th_mul_q30(speed,th_cos(a)),ty=th_mul_q30(speed,th_sin(a));
            spawn(w,w->boss_x,w->boss_y,divide((int64_t)tx*7,2),divide((int64_t)ty*7,2),tx,ty,TH_BALL_RADIAL0+i);
        }
    }
    if(time==280){p->attraction=42949673;wander(w,p,rng,180);}
}
