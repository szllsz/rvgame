/* Continuous Scarlet presentation with two canonical matching measurements.
 * A measurement boundary resets only the meter; attack state continues.
 * Changing backend restarts the same seed/input trace for a real comparison.
 */
#include "scarlet_presentation.h"
ScDemo sc_demo_state;
unsigned sc_demo_paused;
unsigned sc_demo_pause_choice;
static uint16_t selected_seed=2026;
static unsigned measuring;
static uint32_t mix(uint32_t hash,uint32_t value){return (hash^value)*16777619u;}
static void invalidate(ThComparison *p){
    for(unsigned mode=0;mode<2;mode++){
        p->valid[mode]=p->windows[mode]=0;
        for(unsigned i=0;i<2;i++)p->fps100[mode][i]=p->checksums[mode][i]=p->strict[mode][i]=p->missed[mode][i]=0;
    }
}
static void density_step(ThComparison *p,unsigned up){
    const unsigned levels[3]={SC_TIER_LOW,SC_TIER_MID,SC_TIER_HIGH};
    unsigned tier=sc_density_tier(p->groups);
    if(up){if(tier<2)tier++;}else if(tier)tier--;
    if(p->groups!=levels[tier]){p->groups=levels[tier];p->reload=1;invalidate(p);}
}
/* Live input never advances ECL or changes its RNG/projectile workload.
 * Movement invalidates canonical fixed-input comparison windows. */
void sc_demo_move(ThComparison *p,ThGame *g,int32_t x,int32_t y,unsigned focus){
    if(!p->enabled||sc_demo_paused)return;
    if(!x&&!y){g->player.vx=g->player.vy=0;g->player.focused=!!focus;return;}
    int32_t old_x=g->player.x,old_y=g->player.y;
    th_player_move(&g->player,x,y,focus);
    if(g->player.x>368*TH_Q)g->player.x=368*TH_Q;
    if(g->player.y>432*TH_Q)g->player.y=432*TH_Q;
    g->player.vx=g->player.x-old_x;g->player.vy=g->player.y-old_y;
    if(g->player.vx||g->player.vy){invalidate(p);measuring=0;}
}
uint32_t th_comparison_hash(const ThGame *g){
    uint32_t h=mix(2166136261u,g->seed);const ThWorld *w=&g->world;
    h=mix(h,w->frame);h=mix(h,(uint32_t)w->boss_x);h=mix(h,(uint32_t)w->boss_y);
    h=mix(h,w->spawned);h=mix(h,w->retired);h=mix(h,w->overflow);
    if(sc_demo_state.density>1)h=mix(h,sc_demo_state.density);
    h=mix(h,sc_demo_state.rng_draws);h=mix(h,sc_demo_state.seed);
    h=mix(h,sc_demo_state.current.ip);h=mix(h,sc_demo_state.current.time);
    h=mix(h,sc_demo_state.current.child);h=mix(h,sc_demo_state.cycles);
    h=mix(h,(uint32_t)g->player.x);h=mix(h,(uint32_t)g->player.y);
    for(uint16_t slot=g->shot_head;slot!=TH_NONE;slot=g->shots[slot].next){
        const ThShot *shot=&g->shots[slot];h=mix(h,shot->id);h=mix(h,(uint32_t)shot->x);h=mix(h,(uint32_t)shot->y);
        h=mix(h,(uint32_t)shot->vx);h=mix(h,(uint32_t)shot->vy);h=mix(h,shot->age|((uint32_t)shot->kind<<16));
    }
    for(uint16_t at=w->head;at!=TH_NONE;at=w->bullets[at].next){
        const ThBullet *b=&w->bullets[at];h=mix(h,b->id);h=mix(h,(uint32_t)b->x);h=mix(h,(uint32_t)b->y);
        h=mix(h,(uint32_t)b->vx);h=mix(h,(uint32_t)b->vy);h=mix(h,(uint32_t)b->tx);h=mix(h,(uint32_t)b->ty);
        h=mix(h,b->angle);h=mix(h,b->age|((uint32_t)b->kind<<16)|((uint32_t)b->graze_reset<<24));
    }
    return h;
}
void th_comparison_enter(ThComparison *p,uint32_t held){
    *p=(ThComparison){0};p->enabled=1;p->scene=3;p->groups=SC_DEFAULT_DENSITY;p->previous_buttons=held;p->reload=1;
    selected_seed=2026;sc_demo_paused=sc_demo_pause_choice=0;measuring=1;
}
unsigned th_comparison_step(ThComparison *p,ThGame *g,uint32_t buttons){
    if(!p->enabled||p->software>1||p->scene>3||p->groups<1||p->groups>SC_DENSITY_LEVELS)return TH_COMPARE_ERROR;
    uint32_t pressed=buttons&~p->previous_buttons;p->previous_buttons=buttons;
    /* Physical B is action bit0; A is bit1. Keep the existing ABI names. */
    if((pressed&TH_SELECT)||((pressed&TH_A)&&!sc_demo_paused)){p->enabled=0;g->phase=TH_EXIT;return TH_COMPARE_EXIT;}
    if(sc_demo_paused&&(pressed&TH_A)){
        sc_demo_paused=0;measuring=0;p->reload=2;
        /* Cancel consumes this edge; it must not toggle software rendering. */
        pressed&=~(TH_A|TH_B|TH_START);
    }
    if(pressed&TH_START){
        sc_demo_paused^=1u;measuring=0;
        if(sc_demo_paused)sc_demo_pause_choice=0;
        if(!sc_demo_paused)p->reload=2;
    }
    if(sc_demo_paused&&(pressed&TH_B)){sc_demo_paused=0;measuring=0;p->reload=2;}
    else if(!sc_demo_paused&&(pressed&TH_B)){p->software^=1u;p->reload=1;}
    if(sc_demo_paused&&(pressed&0x30u))sc_demo_pause_choice=(sc_demo_pause_choice+(pressed&16u?2u:1u))%3;
    if(sc_demo_paused&&sc_demo_pause_choice==2&&(pressed&0xc0u)){
        p->scene=(p->scene+(pressed&128u?1u:3u))%4;p->reload=1;invalidate(p);
    }
    if(pressed&TH_X){selected_seed=selected_seed>=2028?2026:(uint16_t)(selected_seed+1);p->reload=1;invalidate(p);}
    /* A held trigger changes only once. Simultaneous triggers cancel rather
     * than racing through two levels. Pause arrows select the same 3 tiers. */
    if((buttons&(SC_LT|SC_RT))!=(SC_LT|SC_RT)){
        if(pressed&SC_RT)density_step(p,1);
        else if(pressed&SC_LT)density_step(p,0);
    }
    if(sc_demo_paused&&sc_demo_pause_choice==1){
        if((pressed&0xc0u)==128u)density_step(p,1);
        else if((pressed&0xc0u)==64u)density_step(p,0);
    }
    unsigned flags=0;
    if(p->reload){
        if(p->reload==1){
            sc_demo_init(&sc_demo_state,g,selected_seed);sc_demo_density(&sc_demo_state,p->groups);p->windows[p->software]=0;measuring=1;
        }else if(!sc_demo_paused)sc_demo_tick(&sc_demo_state,g);
        p->cursor=0;p->hash=2166136261u;p->reload=0;p->resets++;flags=TH_COMPARE_RESET;
    }else if(!sc_demo_paused)sc_demo_tick(&sc_demo_state,g);
    if(sc_demo_state.fault)return TH_COMPARE_ERROR;
    p->hash=mix(p->hash,th_comparison_hash(g));return flags;
}
unsigned th_comparison_present(ThComparison *p,uint64_t cycles,uint32_t intervals,uint32_t missed,uint32_t work_over){
    if(!p->enabled||p->software>1||p->cursor>=600)return TH_COMPARE_ERROR;
    if(sc_demo_paused)return 0;
    if(++p->cursor<600)return 0;
    p->reload=2;
    if(!measuring)return 0;
    if(!cycles||intervals!=599){p->invalid_windows++;measuring=0;return 0;}
    unsigned mode=p->software,other=mode^1u,index=p->windows[mode]++;
    /* Keep the first two canonical windows. Continuing animation neither
     * resets the attack nor replaces them with incomparable later epochs. */
    if(index>=2)return 0;
    if(p->valid[other]>index&&p->checksums[other][index]!=p->hash){p->desync++;invalidate(p);return TH_COMPARE_ERROR;}
    uint32_t fps=(uint32_t)(((uint64_t)599*105000000u*100u+cycles/2)/cycles);
    p->fps100[mode][index]=fps;p->checksums[mode][index]=p->hash;
    p->strict[mode][index]=!missed&&!work_over&&fps>=5990&&fps<=6010;p->missed[mode][index]=missed;
    if(p->valid[mode]<=index)p->valid[mode]=index+1;
    return 0;
}
