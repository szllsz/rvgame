/* Separate deterministic workload. All units use the real enemy/weapon/physics
 * paths; normal and original endless generation/economy remain independent. */
#include "fidelity_world.h"
uint32_t bt_showcase_population(uint32_t wave)
{
    /* Total configured population, including bosses. Their own projectiles
     * remain real runtime load, not a fixed or fabricated bullet count. */
    return wave<10?64u:wave<20?85u:100u;
}
static int kind_for(uint32_t index,uint32_t stage)
{
    /* Weighted content pool from actual base-map D5 wave20 group resources.
     * Stages sample early/late groups; only configured workload is scaled,
     * not claimed to replay the original complete 90-second spawn schedule. */
    const BtWaveDef *wave=&bt_wave_defs[19];unsigned total=0;
    int time=stage<10?10:30;
    for(int pass=0;pass<2;pass++){
        if(pass){if(!total)return -1;index%=total;}
        for(int j=0;j<wave->group_count;j++){
        const BtGroupDef *group=&bt_group_defs[bt_group_refs[wave->group_start+j]];
        if(group->p[BT_G_IS_BOSS] || group->p[BT_G_MIN_DIFFICULTY]>5 || group->p[BT_G_MAX_DIFFICULTY]<5 || group->p[BT_G_SPAWN_TIMING]>time)continue;
        for(int k=0;k<group->unit_count;k++){
            const BtWaveUnitDef *unit=&bt_wave_units[group->unit_start+k];unsigned weight=(unsigned)unit->min_n;
            if(unit->type!=1 || unit->enemy<0)continue;
            if(!pass)total+=weight;
            else if(index<weight)return unit->enemy;
            else index-=weight;
        }
        }
    }
    return -1;
}
static void position(uint32_t i,int32_t *x,int32_t *y)
{
    /* Ring along the arena perimeter, away from the initial player body. */
    uint32_t side=i%4,offset=(i/4)*37%1200;
    *x=(int32_t)(160+offset)*256;*y=140*256;
    if(side==1){*x=1880*256;*y=(int32_t)(140+offset)*256;}
    if(side==2){*x=(int32_t)(1880-offset)*256;*y=1390*256;}
    if(side==3){*x=160*256;*y=(int32_t)(1390-offset)*256;}
}
void bt_showcase_start(TatoGame *g)
{
    bt_zero(g,sizeof(*g));g->showcase=1;g->event_base=-1;
    bt_run_init(&g->run,BT_PACIFIST,BT_HAND,5,1,BT_SHOWCASE_SEED);
    g->test_invincible=1;
    g->run.weapon_count=6;
    for(unsigned i=0;i<6;++i)g->run.weapons[i]=(BtOwnedWeapon){BT_HAND,0};
    bt_begin_wave(g);
}
void bt_showcase_wave_init(TatoGame *g)
{
    /* Keep stage1/10/20 scaling; LT/RT selects a stage explicitly.
     * Normal wave durations and economy are independent of this mode. */
    uint32_t stage=g->run.wave;
    g->wave=20;g->remaining=10u*60;g->max_enemies=TATO_ENEMIES;
    g->run.difficulty=5;g->boss_count=2;
    g->x=100*256;g->y=100*256;
    g->all_final_bosses_killed=0;
    for(int i=0;i<g->boss_count;i++)g->bosses[i]=bt_bosses[i];
    uint32_t count=bt_showcase_population(stage);
    for(uint32_t i=0;i<count;++i){TatoEntity *e=&g->enemies[i];int boss=i<(uint32_t)g->boss_count;
        int kind=boss?g->bosses[i]:kind_for(i-(uint32_t)g->boss_count,stage);
        if(kind<0){bt_world_fault(g,2u);return;}
        e->kind=(int8_t)kind;e->type=boss?4:1;e->birth=60;e->loot_allowed=1;position(i,&e->x,&e->y);
        if(boss){e->x=(int32_t)(i?1500:550)*256;e->y=550*256;}
    }
}
int bt_showcase_shift(TatoGame *g,unsigned up)
{
    if(!g->showcase || g->mode!=TATO_PLAY)return 0;
    uint32_t old=g->run.wave,stage=old;
    if(up){if(old<10)stage=10;else if(old<20)stage=20;}
    else {if(old>=20)stage=10;else if(old>=10)stage=1;}
    if(stage==old)return 0;
    /* Restart each selected workload from the same deterministic state,
     * without carrying drops, RNG or accumulated combat state across tiers. */
    g->run.wave=stage;bt_showcase_restart(g);
    return 1;
}
void bt_showcase_restart(TatoGame *g)
{
    uint32_t stage=g->run.wave;
    bt_showcase_start(g);
    if(stage!=1){g->run.wave=stage;bt_begin_wave(g);}
}
void bt_showcase_replenish(TatoGame *g)
{
    uint32_t count=0;
    for(unsigned i=0;i<g->spawn_count;i++)count+=!bt_enemy_defs[g->spawn_queue[i].kind].boss;
    for(unsigned i=0;i<TATO_ENEMIES;++i)count+=!bt_enemy_defs[g->enemies[i].kind].boss && (g->enemies[i].hp>0 || g->enemies[i].birth>0);
    uint32_t target=bt_showcase_population(g->run.wave)-(uint32_t)g->boss_count;
    for(uint32_t i=count;i<target;++i){int32_t x,y;position(i+g->showcase_born,&x,&y);
        bt_request_spawn(g,kind_for(i,g->run.wave),1,x,y,-1,0);if(g->fault)return;}
}
