#include "fidelity_world.h"
#include "fidelity_rules.h"

static int32_t maximum(int32_t a,int32_t b) {return a>b?a:b;}
static int32_t minimum(int32_t a,int32_t b) {return a<b?a:b;}
static int32_t clamp(int32_t a,int32_t lo,int32_t hi) {return maximum(lo,minimum(hi,a));}
static int near_player(const TatoGame *g,int32_t x,int32_t y,int32_t radius)
{return bt_circle_overlap(x-g->x,y-g->y,radius);}
static void random_position(TatoGame *g,int32_t edge,int32_t *x,int32_t *y)
{
    edge=minimum(edge,(TATO_WORLD_HEIGHT*256)/5);
    *x=bt_rand_range(g,edge,TATO_WORLD_WIDTH*256-edge);
    *y=bt_rand_range(g,edge,TATO_WORLD_HEIGHT*256-edge);
}
static void area_position(TatoGame *g,int32_t base_x,int32_t base_y,int32_t area,int32_t edge,int32_t border,int32_t *x,int32_t *y)
{
    if(border) {
        random_position(g,64*256,x,y);
        switch(bt_draw(g,4)) {
            case 0:*x=64*256;break;case 1:*x=(TATO_WORLD_WIDTH-64)*256;break;
            case 2:*y=64*256;break;default:*y=(TATO_WORLD_HEIGHT-64)*256;break;
        }
    } else if(area<0) random_position(g,edge*256,x,y);
    else {
        int32_t half=area*128,e=64*256;
        base_x=clamp(base_x,e+half,TATO_WORLD_WIDTH*256-e-half);
        base_y=clamp(base_y,e+half,TATO_WORLD_HEIGHT*256-e-half);
        *x=bt_rand_range(g,base_x-half,base_x+half);*y=bt_rand_range(g,base_y-half,base_y+half);
    }
}
void bt_request_spawn(TatoGame *g,int32_t kind,int32_t type,int32_t x,int32_t y,int32_t source,int32_t category)
{
    if(kind<0 || kind>=BT_ENEMY_KIND_COUNT) {bt_world_fault(g,2u);return;}
    if(g->spawn_count==BT_SPAWN_CAP) {++g->spawn_overflows;bt_world_fault(g,4u);return;}
    BtSpawn *s=&g->spawn_queue[g->spawn_count++];
    s->kind=(int16_t)kind;s->type=(int16_t)type;s->x=x;s->y=y;s->source=(int16_t)source;s->category=(int16_t)category;
    if(g->spawn_count>g->spawn_peak) g->spawn_peak=g->spawn_count;
}
static BtActiveGroup *add_group(TatoGame *g,int32_t definition,int32_t start,int32_t synthetic,int32_t min_n,int32_t max_n)
{
    if(g->group_count==BT_GROUP_CAP) {++g->group_overflows;bt_world_fault(g,8u);return 0;}
    const BtGroupDef *d=&bt_group_defs[definition];
    BtActiveGroup *a=&g->groups[g->group_count++];
    a->definition=definition;a->next=start>=0?start:d->p[BT_G_SPAWN_TIMING];
    a->remaining=d->p[BT_G_REPEATING];a->interval=d->p[BT_G_REPEATING_INTERVAL];a->first=1;
    a->synthetic_enemy=synthetic;a->min_n=min_n;a->max_n=max_n;
    a->area=d->p[BT_G_AREA];a->border=d->p[BT_G_SPAWN_EDGE_OF_MAP];
    return a;
}
static void init_events(TatoGame *g)
{
    int32_t base=(int32_t)((g->wave-1)/10)*10;
    if(base<10) base=10;
    if(g->event_base==base) return;
    g->event_base=base;g->event_count=0;
    if(g->run.difficulty<2) return;
    int32_t remaining[BT_ELITE_KIND_COUNT];
    for(uint32_t i=0;i<BT_ELITE_KIND_COUNT;++i) remaining[i]=bt_elites[i];
    uint32_t count=BT_ELITE_KIND_COUNT;
    g->event_count=g->run.difficulty<4?1:3;
    for(int32_t i=0;i<g->event_count;++i) {
        int32_t offset=i==0?1:i==1?4:7;
        g->event_wave[i]=base+offset+(int32_t)bt_draw(g,2);
        g->event_type[i]=(i!=2 && base==10 && bt_chance(g,400000))?1:0;
        uint32_t selected=bt_draw(g,count);
        g->event_elite[i]=remaining[selected];
        if(!g->event_type[i]) {remaining[selected]=remaining[--count];}
    }
}
void bt_endless_last_boss_reinforcements(TatoGame *g)
{
    /* main.gd:_on_enemy_died uses six extra groups on endless wave20, staggered one second. */
    for(int32_t i=0;i<6;++i) {
        int32_t enemy=bt_endless_enemies[bt_draw(g,9)];
        int32_t max_n=3+(int32_t)bt_draw(g,2);
        BtActiveGroup *a=add_group(g,bt_endless_groups[0],(int32_t)(g->ticks/60)+i,enemy,2,max_n);
        if(!a) return;
        a->area=bt_draw(g,1000001)>500000?-1:bt_rand_range(g,200,300);
        a->border=bt_draw(g,1000001)>500000;a->interval=bt_round(6.0+2.0*bt_draw(g,1000001)/1000000.0);
    }
}
uint32_t tato_wave_seconds(uint32_t wave)
{return wave<=20?(uint32_t)bt_wave_defs[wave?wave-1:0].duration:60u;}
void bt_waves_init(TatoGame *g)
{
    g->wave=g->run.wave;g->ticks=0;g->remaining=tato_wave_seconds(g->wave)*60;
    g->spawn_count=g->group_count=0;g->early_finish=0;g->horde=0;g->items_spawned_this_wave=0;
    bt_zero(g->enemies,sizeof(g->enemies));bt_zero(g->mob_shots,sizeof(g->mob_shots));
    bt_zero(g->bullets,sizeof(g->bullets));bt_zero(g->drops,sizeof(g->drops));bt_zero(g->consumables,sizeof(g->consumables));
    bt_zero(g->effects,sizeof(g->effects));
    if(g->showcase){bt_showcase_wave_init(g);return;}
    if(g->wave==1) {
        uint32_t index=bt_draw(g,2);g->bosses[0]=bt_bosses[index];g->boss_count=g->run.difficulty==5?2:1;
        g->bosses[1]=bt_bosses[index^1u];g->event_base=-1;g->all_final_bosses_killed=0;
    }
    init_events(g);
    uint32_t wave_index=g->wave-1;
    if(g->wave>20) {wave_index=(g->wave-1)%20;if(wave_index<10) wave_index+=10;}
    const BtWaveDef *w=&bt_wave_defs[wave_index];
    g->max_enemies=g->wave<=20?w->max_enemies:(int32_t)(w->max_enemies*(1.25+wave_index*.01));
    for(int32_t i=0;i<w->group_count;++i) add_group(g,bt_group_refs[w->group_start+i],-1,-1,0,0);
    if(g->wave>20) for(int32_t i=0;i<w->group_count;++i) {
        const BtGroupDef *d=&bt_group_defs[bt_group_refs[w->group_start+i]];
        if(d->p[BT_G_IS_BOSS]) {
            g->boss_count=2;uint32_t first=bt_draw(g,2);
            g->bosses[0]=bt_bosses[first];g->bosses[1]=bt_bosses[first^1u];
            for(uint32_t j=0;j<(g->wave-20+9)/10;++j)
                add_group(g,bt_elite_group[0],0,bt_elites[bt_draw(g,BT_ELITE_KIND_COUNT)],1,1);
        }
    }
    for(int32_t i=0;i<w->conditional_count;++i) {
        const int32_t *c=bt_conditional_refs[w->conditional_start+i];
        add_group(g,bt_group_refs[c[0]+bt_draw(g,(uint32_t)c[1])],-1,-1,0,0);
    }
    for(uint32_t i=0;i<sizeof(bt_groups_data_in_all_waves)/sizeof(bt_groups_data_in_all_waves[0]);++i)
        add_group(g,bt_groups_data_in_all_waves[i],-1,-1,0,0);
    int elite_wave=0;
    for(int32_t i=0;i<g->event_count;++i) if(g->event_wave[i]==(int32_t)g->wave) {
        if(g->event_type[i]) {
            g->horde=1;
            for(uint32_t j=0;j<sizeof(bt_horde_groups)/sizeof(bt_horde_groups[0]);++j) {
                const BtGroupDef *h=&bt_group_defs[bt_horde_groups[j]];
                if((int32_t)g->wave>=h->p[BT_G_MIN_WAVE] && (int32_t)g->wave<=h->p[BT_G_MAX_WAVE])
                    add_group(g,bt_horde_groups[j],-1,-1,0,0);
            }
        } else {
            elite_wave=1;add_group(g,bt_elite_group[0],0,g->event_elite[i],1,1);
            if(g->wave>20) for(uint32_t j=0;j<(g->wave-20+9)/10;++j)
                add_group(g,bt_elite_group[0],0,bt_elites[bt_draw(g,BT_ELITE_KIND_COUNT)],1,1);
        }
    }
    if(g->wave>20) {
        int32_t number=(int32_t)(g->wave/10.0*2),bonus=number/5;
        for(int32_t i=0;i<number;++i) {
            int32_t enemy=bt_endless_enemies[bt_draw(g,sizeof(bt_endless_enemies)/sizeof(bt_endless_enemies[0]))];
            int32_t timing=(int32_t)(i*(50.0/number)+1.0),n=1+bonus;
            BtActiveGroup *a=add_group(g,bt_endless_groups[0],timing,enemy,n,n+bonus+(int32_t)bt_draw(g,2));
            if(!a) return;
            a->border=bt_draw(g,1000001)>500000;
            a->area=bt_draw(g,1000001)>500000?-1:bt_rand_range(g,200,300);
            a->interval=bt_round(6.0+2.0*bt_draw(g,1000001)/1000000.0);
        }
    }
    /* Mark groups filtered by danger and event type without changing their source data. */
    for(uint32_t i=0;i<g->group_count;++i) {
        BtActiveGroup *a=&g->groups[i];const BtGroupDef *d=&bt_group_defs[a->definition];
        if(d->p[BT_G_IS_BOSS]) a->next=0;
        if((int32_t)g->run.difficulty<d->p[BT_G_MIN_DIFFICULTY] || (int32_t)g->run.difficulty>d->p[BT_G_MAX_DIFFICULTY] ||
           (d->p[BT_G_PREVENT_IF_ELITE] && elite_wave) || (d->p[BT_G_PREVENT_IF_HORDE] && g->horde)) a->next=-1;
    }
}
static void trim_original_population(TatoGame *g)
{
    uint32_t candidates[TATO_ENEMIES],priority[TATO_ENEMIES],count=0,p=0;
    for(uint32_t i=0;i<TATO_ENEMIES;++i) if(g->enemies[i].hp>0 && !g->enemies[i].birth && g->enemies[i].type==1) {
        candidates[count++]=i;
        if(bt_enemy_defs[g->enemies[i].kind].priority) priority[p++]=i;
    }
    while(count>(uint32_t)g->max_enemies) {
        uint32_t index;
        if(p) {uint32_t selected=bt_draw(g,p);index=priority[selected];priority[selected]=priority[--p];}
        else index=candidates[bt_draw(g,count)];
        g->enemies[index].loot_allowed=0;bt_enemy_die(g,index,0);++g->original_population_removals;
        for(uint32_t i=0;i<count;++i) if(candidates[i]==index) {candidates[i]=candidates[--count];break;}
    }
}
static void emit_unit(TatoGame *g,const BtActiveGroup *a,int32_t enemy,int32_t type,int32_t min_n,int32_t max_n,int32_t chance,int32_t additional_dist,int32_t base_x,int32_t base_y)
{
    const BtGroupDef *d=&bt_group_defs[a->definition];
    int32_t number=bt_rand_range(g,min_n,max_n+1);
    int64_t scaled=(int64_t)number*chance;
    int32_t count=(int32_t)(scaled/1000000);
    if(bt_chance(g,(int32_t)(scaled%1000000))) ++count;
    for(int32_t i=0;i<count;++i) {
        int32_t x,y,dist=300*256+additional_dist;
        area_position(g,base_x,base_y,a->area,d->p[BT_G_SPAWN_DIST_AWAY_FROM_EDGES],a->border,&x,&y);
        while(near_player(g,x,y,dist)) {
            area_position(g,base_x,base_y,a->area,d->p[BT_G_SPAWN_DIST_AWAY_FROM_EDGES],a->border,&x,&y);
            dist=maximum(25*256,dist-5*256);if(dist==25*256) break;
        }
        int32_t category=d->p[BT_G_IS_BOSS]?2:type==2?1:0;
        bt_request_spawn(g,enemy,type,x,y,-1,category);
    }
}
static void emit_group(TatoGame *g,const BtActiveGroup *a)
{
    const BtGroupDef *d=&bt_group_defs[a->definition];
    trim_original_population(g);
    int32_t x,y,area=a->area,dist=minimum((TATO_WORLD_WIDTH*256)/2,300*256+area*128);
    int32_t edge=(area<0?0:area/2)+d->p[BT_G_SPAWN_DIST_AWAY_FROM_EDGES];
    random_position(g,edge*256,&x,&y);
    while(near_player(g,x,y,dist)) {random_position(g,edge*256,&x,&y);dist=maximum(25*256,dist-5*256);if(dist==25*256) break;}
    if(a->synthetic_enemy>=0) {
        emit_unit(g,a,a->synthetic_enemy,d->p[BT_G_IS_BOSS]?4:1,a->min_n,a->max_n,1000000,0,x,y);return;
    }
    if(d->p[BT_G_IS_BOSS]) {
        if(g->wave>=20) for(int32_t i=0;i<g->boss_count;++i)
            emit_unit(g,a,g->bosses[i],4,1,1,1000000,0,x,y);
        return;
    }
    for(int32_t i=0;i<d->unit_count;++i) {
        const BtWaveUnitDef *u=&bt_wave_units[d->unit_start+i];
        emit_unit(g,a,u->enemy,u->type,u->min_n,u->max_n,u->chance,u->min_distance,x,y);
    }
}
static void spawn_one(TatoGame *g,int32_t category)
{
    for(uint32_t i=g->spawn_count;i>0;--i) if(g->spawn_queue[i-1].category==category) {
        BtSpawn s=g->spawn_queue[i-1];
        for(uint32_t j=i;j<g->spawn_count;++j) g->spawn_queue[j-1]=g->spawn_queue[j];
        --g->spawn_count;
        for(uint32_t j=0;j<TATO_ENEMIES;++j) if(g->enemies[j].hp<=0 && !g->enemies[j].birth) {
            TatoEntity *e=&g->enemies[j];bt_zero(e,sizeof(*e));e->x=s.x;e->y=s.y;e->kind=s.kind;e->type=s.type;e->birth=60;
            e->loot_allowed=1;return;
        }
        ++g->entity_overflows;bt_world_fault(g,16u);return;
    }
}
void bt_waves_tick(TatoGame *g)
{
    bt_structures_schedule(g);if(g->fault)return;
    if(g->showcase && g->ticks%60==1){bt_showcase_replenish(g);if(g->fault)return;}
    if(g->ticks%60==1) {
        int32_t elapsed=(int32_t)(g->ticks/60);
        for(uint32_t i=0;i<g->group_count;++i) {
            BtActiveGroup *a=&g->groups[i];if(a->next<0 || a->next>elapsed) continue;
            const BtGroupDef *d=&bt_group_defs[a->definition];
            if(!a->first || bt_chance(g,d->chance)) emit_group(g,a);
            if(a->first) {a->first=0;if(a->remaining<=0) {a->next=-1;continue;}}
            else {
                if(--a->remaining<=0) {a->next=-1;continue;}
                a->interval=maximum(d->p[BT_G_MIN_REPEATING_INTERVAL],a->interval-d->p[BT_G_REDUCE_REPEATING_INTERVAL]);
            }
            a->next+=a->interval;
        }
    }
    if(g->ticks%3==0) {
        bt_structures_spawn_tick(g);if(g->fault)return;
        spawn_one(g,1);spawn_one(g,2);spawn_one(g,3);
        uint32_t normal=0;for(uint32_t i=0;i<g->spawn_count;++i) normal+=g->spawn_queue[i].category==0;
        uint32_t count=normal>=100?(uint32_t)clamp((int32_t)(normal-100)/10,1,2):1;
        while(count--) spawn_one(g,0);
    }
    uint32_t births=0;
    for(uint32_t i=0;i<TATO_ENEMIES;++i) {
        TatoEntity *e=&g->enemies[i];if(!e->birth) continue;
        ++births;
        if(--e->birth==0) {
            /* Original birth circle radius 72 against player body radius 24 at y=-12. */
            if(e->type==1 && bt_circle_overlap(e->x-g->x,e->y-g->y+12*256,96*256)) {random_position(g,64*256,&e->x,&e->y);e->birth=60;}
            else bt_enemy_activate(g,i);
        }
    }
    if(births>g->birth_peak) g->birth_peak=births;
}
