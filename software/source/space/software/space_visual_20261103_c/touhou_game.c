/* Core source rules from Taisei1.4.6 player.c / reimu_a.c / boss.c.
 * Fixed rank0 deliberately omits bombs, power items and slave controllers.
 * Position/velocity are fixed; rare boss HP arithmetic preserves float32. */
#include "touhou_game.h"
#include "touhou_collision.h"
static void zero_game(ThGame *g){uint8_t *p=(uint8_t *)g;for(uint32_t i=0;i<sizeof(*g);i++)p[i]=0;}
int th_game_alive(const ThGame *g){return g->death_at<(int32_t)g->clock&&g->clock>=(uint32_t)g->respawn_at;}
static int vulnerable(const ThGame *g){return !g->invincible&&th_game_alive(g)&&g->clock>=(uint32_t)g->recovery_at;}
void th_game_start(ThGame *g,uint32_t seed,uint32_t held){
    zero_game(g);th_pattern_init(&g->world,&g->attack,&g->rng,seed);th_player_init(&g->player);
    g->seed=seed;g->previous_buttons=held;g->phase=TH_ENTRY;
    /* Source timeline boss_appear, replacing the omitted dialog by a bounded
     *180-tick entry. Player movement/shots remain normal; Boss is invulnerable. */
    g->world.boss_x=-230*TH_Q;g->world.boss_y=100*TH_Q;
    g->attack.boss_vx=th_mul_q30(470*TH_Q,53687091);g->world.ended=1;
    g->lives=2;g->power=0;g->death_at=-1;g->boss_hp=g->boss_max_hp=26000.0f;g->forward_side=1;
    g->shot_head=g->shot_tail=TH_NONE;
    for(unsigned i=0;i<TH_SHOT_POOL;i++)g->shots[i].next=(uint16_t)(i+1);
    g->shots[TH_SHOT_POOL-1].next=TH_NONE;
}
void th_game_init(ThGame *g,uint32_t seed,uint32_t held){th_game_start(g,seed,held);g->phase=TH_TITLE;}
static void restart(ThGame *g,uint32_t held){
    unsigned invincible=g->invincible,immortal=g->boss_immortal;
    th_game_start(g,g->seed,held);g->invincible=(uint8_t)invincible;g->boss_immortal=(uint8_t)immortal;
}
static void hurt(ThGame *g){
    if(!vulnerable(g))return;
    g->death_at=(int32_t)g->clock+12;g->damage_events++;g->events|=TH_EVENT_HURT;g->spell_failed=1;
}
static unsigned collide(void *context,ThBullet *b,int32_t old_x,int32_t old_y){
    ThGame *g=context;uint32_t before=g->player.graze;
    unsigned event=th_enemy_collision(&g->player,b,old_x,old_y);
    if(event==1)hurt(g);
    if(g->player.graze!=before)g->events|=TH_EVENT_GRAZE;
    /* Invincibility also preserves the visible enemy projectile. It must not
     * reduce the presentation workload by removing every touching bullet. */
    return event==1&&g->invincible?0:event;
}
static void shot_spawn(ThGame *g,unsigned kind,int32_t x,int32_t y,int32_t vx,int32_t vy,unsigned damage){
    uint16_t slot=g->shot_free;
    if(slot==TH_NONE){g->shot_overflow++;return;}
    ThShot *s=&g->shots[slot];g->shot_free=s->next;
    s->x=x;s->y=y;s->vx=vx;s->vy=vy;s->id=++g->next_shot_id;
    s->angle=th_angle(vx,vy);s->age=0;s->damage=(uint16_t)damage;s->kind=(uint8_t)kind;s->next=TH_NONE;
    if(g->shot_tail==TH_NONE)g->shot_head=slot;else g->shots[g->shot_tail].next=slot;
    g->shot_tail=slot;g->shot_active++;g->shots_spawned++;g->events|=TH_EVENT_SHOT;
    if(g->shot_active>g->shot_peak)g->shot_peak=g->shot_active;
}
static void shot_release(ThGame *g,uint16_t previous,uint16_t slot){
    ThShot *s=&g->shots[slot];uint16_t next=s->next;
    if(previous==TH_NONE)g->shot_head=next;else g->shots[previous].next=next;
    if(g->shot_tail==slot)g->shot_tail=previous;
    s->kind=0;s->next=g->shot_free;g->shot_free=slot;g->shot_active--;
}
static void boss_damage(ThGame *g,unsigned amount){
    int32_t time=(int32_t)g->world.frame-TH_ATTACK_DELAY;
    float factor=time<=60?0.0f:time>=300?1.0f:(float)(time-60)/240.0f;
    if(factor==0.0f)return;
    float damage=(float)amount*factor;
    if(damage>g->boss_hp)damage=g->boss_hp;
    if(!g->boss_immortal)g->boss_hp-=damage;
    g->damage_dealt+=damage;g->events|=TH_EVENT_HIT;
}
static void shots_update(ThGame *g){
    uint16_t previous=TH_NONE,slot=g->shot_head;
    while(slot!=TH_NONE){
        ThShot *s=&g->shots[slot];uint16_t next=s->next;s->age++;s->x+=s->vx;s->y+=s->vy;
        int32_t dx=s->x-g->world.boss_x/8,dy=s->y-g->world.boss_y/8;
        int hit=!g->world.ended&&((int64_t)dx*dx+(int64_t)dy*dy<(int64_t)(42*TH_SHOT_Q)*(42*TH_SHOT_Q));
        /* pp_ofuda16x26 and pp_hakurei_seal16x39; angle/texture scale do not
         * alter source player-projectile OOB size. Collision is a point test. */
        int32_t half_y=s->kind==1?13*TH_SHOT_Q:39*TH_SHOT_Q/2;
        int outside=s->x< -8*TH_SHOT_Q||s->x>488*TH_SHOT_Q||s->y< -half_y||s->y>560*TH_SHOT_Q+half_y;
        if(hit||outside){
            if(hit){boss_damage(g,s->damage);g->shots_hit++;}else g->shots_retired++;
            shot_release(g,previous,slot);
        }else previous=slot;
        slot=next;
    }
}
static void shoot(ThGame *g){
    if(g->clock>=g->forward_at){
        int side=g->forward_side?1:-1;g->forward_side^=1u;
        shot_spawn(g,1,g->player.x/8+side*10*TH_SHOT_Q,g->player.y/8-15*TH_SHOT_Q,0,-20*TH_SHOT_Q,50);
        g->forward_at=g->clock+3;
    }
    if(g->clock>=g->volley_at){
        /* Rank0 has exactly this pair, spread pi/32, speed18, damage35. */
        int32_t vx=th_mul_q30(18*TH_SHOT_Q,th_sin(0x04000000u));
        int32_t vy=-th_mul_q30(18*TH_SHOT_Q,th_cos(0x04000000u));
        shot_spawn(g,2,g->player.x/8+5*TH_SHOT_Q,g->player.y/8-TH_SHOT_Q,vx,vy,35);
        shot_spawn(g,2,g->player.x/8-5*TH_SHOT_Q,g->player.y/8-TH_SHOT_Q,-vx,vy,35);
        g->volley_at=g->clock+16;
    }
}
static void life(ThGame *g,uint32_t buttons){
    if(g->respawn_at>(int32_t)g->clock){
        if(g->lives<0&&g->clock==(uint32_t)(g->respawn_at-30)){
            g->phase=TH_LOSE;g->events|=TH_EVENT_LOSE;
        }
        int32_t u=(int32_t)(((int64_t)(g->respawn_at-(int32_t)g->clock)*1073741824+30)/60);
        int32_t square=th_mul_q30(u,u);
        int32_t smooth=(int32_t)(((int64_t)square*(INT64_C(3221225472)-2*(int64_t)u)+536870912)>>30);
        g->player.x=240*TH_Q;g->player.y=496*TH_Q+th_mul_q30(128*TH_Q,smooth);
        th_clear(&g->world);return;
    }
    if((buttons&TH_A)&&th_game_alive(g))shoot(g);
    if(g->death_at==(int32_t)g->clock){
        g->respawn_at=(int32_t)g->clock+60;g->recovery_at=(int32_t)g->clock+210;
        g->player.x=240*TH_Q;g->player.y=590*TH_Q;g->lives--;g->power=g->power*7/10;
        g->death_events++;g->events|=TH_EVENT_DEATH;th_clear(&g->world);
    }else if(g->death_at>(int32_t)g->clock)th_clear(&g->world);
}
/* Presentation auto-fire shares the existing bounded projectile pool and
 * hit/retirement rules. It never advances the normal stage or enemy RNG. */
void th_game_presentation_shots(ThGame *g){
    g->events=0;shots_update(g);shoot(g);
}
static void finish(ThGame *g,unsigned timeout){
    g->world.ended=1;th_clear(&g->world);g->attack_completed++;
    g->phase=g->attack_index<2?TH_BETWEEN:TH_OUTRO;
    g->finish_at=g->clock+(g->attack_index==0?20:g->attack_index==1?60:120);
    if(timeout){g->timeout++;g->timeout_mask|=1u<<g->attack_index;g->spell_failed=1;}
    else g->kill_mask|=1u<<g->attack_index;
}
static void next_attack(ThGame *g){
    int32_t x=g->world.boss_x,y=g->world.boss_y;g->attack_index++;
    if(g->attack_index==1){
        th_rain_init(&g->world,&g->attack);g->world.boss_x=x;g->world.boss_y=y;
        g->attack.target_x=x;g->attack.target_y=y;g->boss_hp=g->boss_max_hp=33000.0f;
    }else{
        th_init(&g->world);g->world.boss_x=x;g->world.boss_y=y;
        g->attack=(ThPattern){0};g->attack.attraction=10737418;
        g->attack.target_x=240*TH_Q;g->attack.target_y=120*TH_Q;
        g->attack.boss_vx=th_mul_q30(g->attack.target_x-x,g->attack.attraction);
        g->world.boss_vy=th_mul_q30(g->attack.target_y-y,g->attack.attraction);
        g->boss_hp=g->boss_max_hp=40000.0f;
    }
    g->phase=TH_PLAY;
}
void th_game_tick(ThGame *g,int32_t x,int32_t y,uint32_t buttons){
    uint32_t previous=g->previous_buttons,pressed=buttons&~previous;g->previous_buttons=buttons;g->events=0;
    if(pressed&TH_SELECT){g->phase=TH_EXIT;return;}
    if(g->phase==TH_EXIT)return;
    if(g->boss_immortal)g->invincible=1;
    else if((buttons&(TH_LB|TH_RB))==(TH_LB|TH_RB)&&(previous&(TH_LB|TH_RB))!=(TH_LB|TH_RB)){
        g->invincible^=1u;
        if(g->invincible&&g->death_at>=(int32_t)g->clock&&g->respawn_at<=(int32_t)g->clock)g->death_at=-1;
    }
    if(g->phase==TH_TITLE){if(pressed&TH_A)restart(g,buttons);return;}
    if(g->phase==TH_PAUSE){
        if(pressed&(TH_START|TH_B))g->phase=g->resume_phase;
        else if(pressed&TH_Y)restart(g,buttons);
        return;
    }
    if(g->phase==TH_WIN||g->phase==TH_LOSE){if(pressed&(TH_A|TH_Y))restart(g,buttons);return;}
    if(pressed&TH_START){g->resume_phase=g->phase;g->phase=TH_PAUSE;return;}
    g->clock++;
    if(th_game_alive(g))th_player_move(&g->player,x,y,buttons&TH_X);
    else g->player.vx=g->player.vy=0;
    if(g->phase==TH_ENTRY){
        g->world.boss_x+=g->attack.boss_vx;g->world.boss_y+=g->world.boss_vy;
        g->attack.boss_vx=th_mul_q30(240*TH_Q-g->world.boss_x,53687091);
        g->world.boss_vy=th_mul_q30(100*TH_Q-g->world.boss_y,53687091);
        shots_update(g);life(g,buttons);g->entry_elapsed++;
        if(g->entry_elapsed==180){
            int32_t bx=g->world.boss_x,by=g->world.boss_y;
            th_init(&g->world);g->world.boss_x=bx;g->world.boss_y=by;
            g->world.boss_vy=th_mul_q30(100*TH_Q-by,53687091);
            g->phase=TH_PLAY;
        }
        return;
    }
    if(g->phase==TH_PLAY&&g->boss_hp<=0.0f)finish(g,0);
    unsigned duration=g->attack_index==0?1200:2400;
    if(!g->world.ended&&g->world.frame+1>=TH_ATTACK_DELAY&&g->world.frame+1<=TH_ATTACK_DELAY+duration){
        int32_t dx=g->player.x-(g->world.boss_x+g->attack.boss_vx),dy=g->player.y-(g->world.boss_y+g->world.boss_vy);
        if((int64_t)dx*dx+(int64_t)dy*dy<(int64_t)(16*TH_Q)*(16*TH_Q))hurt(g);
    }
    if(g->attack_index<2)th_pattern_update(&g->world,&g->attack,collide,g);
    else{
        if(!g->world.ended){g->world.boss_x+=g->attack.boss_vx;g->attack.boss_vx=th_mul_q30(240*TH_Q-g->world.boss_x,10737418);}
        th_update_checked(&g->world,collide,g);
    }
    shots_update(g);
    /* Same full-stage task order: player logic is older than the Boss attack.
     * Shot creation/clear follows old projectile processing, before emit. */
    life(g,buttons);
    if(g->attack_index<2)th_pattern_emit(&g->world,&g->attack,&g->rng,g->player.x,g->player.y);
    else th_emit(&g->world);
    if(g->phase==TH_PLAY&&g->world.ended)finish(g,1);
    if(g->phase==TH_BETWEEN&&g->clock>=g->finish_at)next_attack(g);
    if(g->phase==TH_OUTRO&&g->clock>=g->finish_at){g->phase=TH_WIN;g->events|=TH_EVENT_WIN;}
}
