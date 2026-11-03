/* Fixed-point local New Classic final-spell adaptation. See scarlet_demo.h.
 * Selected data comes from generated scarlet_script_data.h. Engine behavior
 * references GensokyoClub/th06 (CC0), not a claim of original authorship.
 */
#include "scarlet_demo.h"
#include "scarlet_script_data.h"

static uint16_t random16(ScDemo *s){
    uint16_t a=(uint16_t)((s->seed^0x9630u)-0x6553u);
    s->seed=(uint16_t)((a<<2)|(a>>14));s->rng_draws++;return s->seed;
}
static uint32_t random32(ScDemo *s){uint32_t high=random16(s);return (high<<16)|random16(s);}
static uint32_t random_angle(ScDemo *s){return random32(s)-0x80000000u;}
static int32_t scaled(int32_t v,int32_t n,int32_t d){return (int32_t)((int64_t)v*n/d);}
static int32_t from_q30(int64_t value){return value>=0?(int32_t)((value+256)/512):-(int32_t)((-value+256)/512);}
static int32_t clamp(int32_t value,int32_t low,int32_t high){return value<low?low:value>high?high:value;}
static const ScInsn *instruction(ScDemo *s){
    unsigned count=s->current.child?sizeof(sc_sub59)/sizeof(*sc_sub59):sizeof(sc_sub66)/sizeof(*sc_sub66);
    if(s->current.ip>=count){s->fault=1;return 0;}
    return &(s->current.child?sc_sub59:sc_sub66)[s->current.ip];
}
unsigned sc_demo_pool(const ScDemo *s){
    unsigned level=s->density?s->density:1u;
    unsigned count=SC_SOURCE_POOL+(level-1u)*256u;
    return count<TH_CAPACITY?count:TH_CAPACITY;
}
void sc_demo_density(ScDemo *s,unsigned level){
    if(level<1||level>SC_DENSITY_LEVELS){s->fault=10;return;}
    s->density=(uint8_t)level;
}
static ThBullet *allocate(ScDemo *s,ThGame *g){
    for(unsigned i=0;i<sc_demo_pool(s);i++){
        unsigned at=s->next_slot;if(++s->next_slot==sc_demo_pool(s))s->next_slot=0;
        ThBullet *b=&g->world.bullets[at];
        if(!b->kind){*b=(ThBullet){0};b->id=++g->world.next_id;g->world.spawned++;return b;}
    }
    s->rejected++;return 0;
}
static void circle(ScDemo *s,ThGame *g,const ScInsn *ins){
    const int32_t *a=ins->a;
    if(a[0]!=9||a[1]!=1||a[2]<1||a[3]<1||a[2]>64||a[3]>8){s->fault=2;return;}
    uint32_t base=a[6]==-10005?s->current.angle:(uint32_t)a[6];
    unsigned spokes=((unsigned)a[2]*((s->density?s->density:1u)+3u)+3u)/4u;
    for(int layer=0;layer<a[3];layer++)for(unsigned i=0;i<spokes;i++){
        ThBullet *b=allocate(s,g);if(!b)continue;
        uint32_t angle=base+(uint32_t)(((uint64_t)(unsigned)i<<32)/spokes)+(uint32_t)(layer*a[7]);
        int32_t speed=a[4]-scaled(a[4]-a[5],layer,a[3]);
        b->x=g->world.boss_x;b->y=g->world.boss_y-12*TH_Q;
        b->angle=angle;b->tx=speed;b->ty=s->effect_acceleration;
        b->vx=th_mul_q30(speed,th_cos(angle));b->vy=th_mul_q30(speed,th_sin(angle));
        b->kind=SC_BUBBLE;b->graze_count=(a[8]&32u)!=0;
        b->graze_cooldown=(uint8_t)s->effect_duration;
        /* Only this attack's +/-1/256-turn polar velocity is used. */
        b->graze_reset=s->effect_angle<0?1:0;
    }
}
static void trail(ScDemo *s,ThGame *g){
    unsigned changed=0;s->trail_calls++;
    for(unsigned i=0;i<sc_demo_pool(s);i++){
        ThBullet *parent=&g->world.bullets[i];if(parent->kind!=SC_BUBBLE)continue;
        int32_t x=parent->x,y=parent->y;uint32_t angle=random_angle(s);
        ThBullet *b=allocate(s,g);
        if(b){b->x=x;b->y=y;b->angle=angle;b->kind=SC_STILL;b->graze_reset=SC_TRAIL_SPAWN_TICKS;}
        changed++;
    }
    s->current.int3=(int32_t)changed;
}
static void release_trails(ScDemo *s,ThGame *g,unsigned mode){
    uint32_t base=random_angle(s);s->release_calls++;
    for(unsigned i=0;i<sc_demo_pool(s);i++){
        ThBullet *b=&g->world.bullets[i];if(b->kind!=SC_STILL)continue;
        uint32_t angle;
        if(mode==11)angle=random_angle(s);
        else{
            int64_t dx=(int64_t)g->world.boss_x-b->x,dy=(int64_t)g->world.boss_y-b->y;
            uint32_t distance=(uint32_t)th_root64((uint64_t)(dx*dx+dy*dy));
            /* distance*pi/256 radians = distance/512 turns. */
            angle=base+distance*4u;
        }
        b->kind=SC_MOVING;b->age=0;b->graze_cooldown=120;
        /* Q30 acceleration plus analytic accumulated displacement prevents
         * Q21 rounding on each velocity update from advancing OOB by a tick. */
        b->tx=th_mul_q30(10737418,th_cos(angle));b->ty=th_mul_q30(10737418,th_sin(angle));
    }
}
static void move_start(ScDemo *s,ThGame *g,unsigned duration){
    if(!duration||duration>240){s->fault=3;return;}
    s->move_x=g->world.boss_x;s->move_y=g->world.boss_y;
    s->move_dx=scaled(th_mul_q30(s->boss_speed,th_cos(s->boss_angle)),(int32_t)duration,2);
    s->move_dy=scaled(th_mul_q30(s->boss_speed,th_sin(s->boss_angle)),(int32_t)duration,2);
    s->move_remaining=s->move_duration=(uint16_t)duration;
}
static void move_update(ScDemo *s,ThGame *g){
    if(s->move_remaining){
        unsigned left=--s->move_remaining,total=s->move_duration;
        int32_t x=s->move_x+s->move_dx-scaled(s->move_dx,(int32_t)(left*left),(int32_t)(total*total));
        int32_t y=s->move_y+s->move_dy-scaled(s->move_dy,(int32_t)(left*left),(int32_t)(total*total));
        s->axis_x=x-g->world.boss_x;s->axis_y=y-g->world.boss_y;
        if(!left){g->world.boss_x=x;g->world.boss_y=y;s->axis_x=s->axis_y=0;}
    }
}
static void script_tick(ScDemo *s,ThGame *g){
    unsigned processed=0;
    while(!s->fault){
        if(processed++==256){s->fault=10;break;}
        const ScInsn *ins=instruction(s);if(!ins)return;
        if(ins->tick>s->current.time)break;
        const int32_t *a=ins->a;unsigned next=s->current.ip+1;
        switch(ins->op){
        case 2:if(!s->current.child&&a[1]==0)s->cycles++;s->current.time=(uint16_t)a[0];next=(unsigned)a[1];break;
        case 3:if(--s->current.counter0>0){s->current.time=(uint16_t)a[0];next=(unsigned)a[1];}break;
        case 4:if(a[0]!=-10009){s->fault=4;break;}s->current.counter0=a[1];break;
        case 9:s->current.angle=random_angle(s);break;
        case 18:s->current.counter3++;break;
        case 27:break; /* sub59 compares int3 to zero, consumed by op34 below */
        case 34:if(s->current.int3!=0){s->current.time=(uint16_t)a[0];next=(unsigned)a[1];}break;
        case 35:
            if(s->in_call||a[0]!=59){s->fault=5;break;}
            s->current.ip=(uint16_t)next;s->saved=s->current;s->in_call=1;
            s->current.child=1;s->current.time=0;s->current.angle=0;next=0;break;
        case 36:
            if(!s->in_call){s->fault=6;break;}
            s->current=s->saved;s->in_call=0;next=s->current.ip;break;
        case 47:s->boss_speed=a[0];break;
        case 50:s->boss_angle=random_angle(s);break;
        case 61:move_start(s,g,(unsigned)a[0]);break;
        case 70:circle(s,g,ins);break;
        case 82:
            if(a[0]<1||a[0]>255){s->fault=7;break;}
            s->effect_duration=(uint16_t)a[0];s->effect_acceleration=a[4];s->effect_angle=a[5];break;
        case 106:break; /* source sound cue, no change to gameplay RNG */
        case 121:if(a[0]==8)trail(s,g);else if(a[0]==9||a[0]==11)release_trails(s,g,(unsigned)a[0]);else s->fault=8;break;
        default:s->fault=9;break;
        }
        s->current.ip=(uint16_t)next;
    }
    s->current.time++;
}
static void bullets_tick(ScDemo *s,ThGame *g){
    ThWorld *w=&g->world;w->head=w->tail=TH_NONE;w->active=0;
    for(unsigned i=0;i<sc_demo_pool(s);i++){
        ThBullet *b=&w->bullets[i];if(!b->kind)continue;unsigned check_bounds=1;
        if(b->kind==SC_BUBBLE){
            if(b->graze_count&&b->graze_cooldown){
                b->graze_cooldown--;b->angle+=b->graze_reset?0xff000000u:0x01000000u;
                int32_t speed=b->tx+from_q30((int64_t)(b->age+1)*b->ty);
                b->vx=th_mul_q30(speed,th_cos(b->angle));b->vy=th_mul_q30(speed,th_sin(b->angle));
            }
            b->x+=b->vx;b->y+=b->vy;
        }else if(b->graze_reset){b->graze_reset--;check_bounds=0;}
        else{
            if(b->kind==SC_MOVING&&b->graze_cooldown){
                int32_t t=121-b->graze_cooldown;int64_t now=(int64_t)t*(t+1)/2,previous=(int64_t)(t-1)*t/2;
                b->x+=from_q30(now*b->tx)-from_q30(previous*b->tx);
                b->y+=from_q30(now*b->ty)-from_q30(previous*b->ty);
                b->vx=from_q30((int64_t)t*b->tx);b->vy=from_q30((int64_t)t*b->ty);b->graze_cooldown--;
            }else{b->x+=b->vx;b->y+=b->vy;}
        }
        b->age++;int32_t half=(b->kind==SC_BUBBLE?32:8)*TH_Q;
        if(check_bounds&&(b->x < -half||b->x>384*TH_Q+half||b->y < -half||b->y>448*TH_Q+half)){b->kind=0;w->retired++;continue;}
        b->next=TH_NONE;if(w->tail!=TH_NONE)w->bullets[w->tail].next=(uint16_t)i;else w->head=(uint16_t)i;
        w->tail=(uint16_t)i;w->active++;
    }
    if(w->active>w->peak)w->peak=w->active;
    w->overflow=s->rejected;
    /* Preserve the existing snapshot's disjoint active/free-list invariant,
     * including the reserved slots above the source engine's 640-slot pool. */
    w->free_head=TH_NONE;
    for(unsigned i=TH_CAPACITY;i>0;i--)if(!w->bullets[i-1].kind){
        w->bullets[i-1].next=w->free_head;w->free_head=(uint16_t)(i-1);
    }
}
void sc_demo_init(ScDemo *s,ThGame *g,uint16_t seed){
    *s=(ScDemo){0};s->seed=seed;
    th_game_init(g,seed,0);th_init(&g->world);
    g->world.boss_x=192*TH_Q;g->world.boss_y=128*TH_Q;g->world.boss_vy=0;
    g->phase=TH_PLAY;g->invincible=g->boss_immortal=1;g->boss_hp=g->boss_max_hp=5300;
    g->player.x=192*TH_Q;g->player.y=400*TH_Q;
}
void sc_demo_tick(ScDemo *s,ThGame *g){
    if(s->fault)return;
    /* Source enemy movement precedes ECL; bullets update after enemy/ECL. */
    g->world.boss_x=clamp(g->world.boss_x+s->axis_x,32*TH_Q,352*TH_Q);
    g->world.boss_y=clamp(g->world.boss_y+s->axis_y,48*TH_Q,120*TH_Q);
    script_tick(s,g);move_update(s,g);
    g->world.boss_x=clamp(g->world.boss_x,32*TH_Q,352*TH_Q);
    g->world.boss_y=clamp(g->world.boss_y,48*TH_Q,120*TH_Q);
    bullets_tick(s,g);s->tick++;g->world.frame=g->clock=s->tick;g->rng.draws=s->rng_draws;
    th_game_presentation_shots(g);
    g->attack_completed=s->cycles;g->world.ended=0;
}
