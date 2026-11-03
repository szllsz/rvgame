#include "fidelity_world.h"
#include "fidelity_geometry.h"
#include "fidelity_rules.h"
static int32_t maximum(int32_t a,int32_t b) {return a>b?a:b;}
static int32_t minimum(int32_t a,int32_t b) {return a<b?a:b;}
static void drop(TatoGame *g,BtDrop *d,int32_t x,int32_t y,int32_t kind)
{
    bt_zero(d,sizeof(*d));d->x=x;d->y=y;d->value=1;d->active=1;d->kind=kind;
    d->idle=10;d->push=1;d->speed=500;
    int32_t spread=bt_rand_range(g,50,100)*128;
    d->target_x=x+bt_rand_range(g,-spread,spread);d->target_y=y+bt_rand_range(g,-spread,spread);
    if(d->target_x<0)d->target_x=0;
    if(d->target_y<0)d->target_y=0;
    if(d->target_x>TATO_WORLD_WIDTH*256)d->target_x=TATO_WORLD_WIDTH*256;
    if(d->target_y>TATO_WORLD_HEIGHT*256)d->target_y=TATO_WORLD_HEIGHT*256;
}
void bt_spawn_garden_fruit(TatoGame *g,int32_t x,int32_t y)
{
    uint32_t slot;for(slot=0;slot<BT_CONSUMABLE_CAP;++slot)if(!g->consumables[slot].active)break;
    if(slot==BT_CONSUMABLE_CAP){++g->consumable_overflows;bt_world_fault(g,32u);return;}
    BtDrop *d=&g->consumables[slot];bt_zero(d,sizeof(*d));d->x=x;d->y=y;d->value=d->active=1;d->push=1;d->idle=10;d->speed=500;
    int32_t distance=bt_rand_range(g,100*256,150*256);
    d->target_x=x+bt_rand_range(g,-distance,distance);d->target_y=y+bt_rand_range(g,-distance,distance);
}
void bt_spawn_gold(TatoGame *g,int32_t value1000,int32_t x,int32_t y)
{
    int32_t count=value1000/1000;
    if(bt_chance(g,(value1000%1000)*1000)) ++count;
    for(int32_t i=0;i<count;++i) {
        uint32_t index;
        for(index=0;index<TATO_DROPS;++index) if(!g->drops[index].active) break;
        if(index==TATO_DROPS) {
            ++g->drops[bt_draw(g,TATO_DROPS)].value;++g->drop_merges;
        } else {
            drop(g,&g->drops[index],x,y,0);
            if(g->run.bonus_gold>0) {++g->drops[index].value;g->drops[index].kind=1;--g->run.bonus_gold;}
        }
    }
}
void bt_enemy_loot(TatoGame *g,uint32_t index)
{
    TatoEntity *e=&g->enemies[index];const BtEnemyDef *d=&bt_enemy_defs[e->kind];
    if(!e->loot_allowed) return;
    if(d->stats[BT_E_CAN_DROP_CONSUMABLES]) {
        double luck=1+g->run.stat100[BT_LUCK]/10000.0;
        double probability=d->stats[BT_E_BASE_DROP_CHANCE]*luck;
        if(probability>1000) probability=1000;
        if(g->wave>20) probability/=1+bt_endless_factor((int32_t)g->wave);
        int32_t always=d->stats[BT_E_ALWAYS_DROP_CONSUMABLES];
        if(bt_chance(g,(int32_t)(probability*1000)) || always) {
            int32_t tier=bt_rand_range(g,d->stats[BT_E_MIN_CONSUMABLE_TIER]/1000,d->stats[BT_E_MAX_CONSUMABLE_TIER]/1000+1);
            double chance=d->stats[BT_E_ITEM_DROP_CHANCE]*luck/(1+g->items_spawned_this_wave);
            if(always && d->stats[BT_E_ITEM_DROP_CHANCE]>=1000 && g->wave<=20) chance=1000;
            if(bt_chance(g,(int32_t)(chance*1000))) tier=d->boss && g->wave<=20?3:1;
            uint32_t slot;
            for(slot=0;slot<BT_CONSUMABLE_CAP;++slot) if(!g->consumables[slot].active) break;
            if(slot==BT_CONSUMABLE_CAP) {++g->consumable_overflows;bt_world_fault(g,32u);return;}
            drop(g,&g->consumables[slot],e->x,e->y,tier==3?3:tier==1?1:0);
            if(tier==1 || tier==3) ++g->items_spawned_this_wave;
        }
    }
    int32_t chance=g->wave<5?1000000:maximum(500000,1000000-(int32_t)g->wave*15000);
    if(g->horde) chance=chance*65/100;
    if(d->stats[BT_E_ALWAYS_DROP_CONSUMABLES]) chance=1000000;
    if(e->type==1 && !bt_chance(g,chance)) return;
    int32_t value=d->stats[BT_E_VALUE];
    if(d->special==BT_SPECIAL_EVIL_MOB) value=e->gold_eaten*(e->evolution+1)*1000;
    bt_spawn_gold(g,value,e->x,e->y);
}
static void tick_drop(TatoGame *g,BtDrop *d,int consumable)
{
    if(!d->active) return;
    int32_t scale=consumable?1000:d->kind?1250+(d->value-2)*50:1000+(d->value-1)*50;
    scale=minimum(scale,2000);
    const BtShapeDef rectangle={2,16*256*scale/1000,16*256*scale/1000,0,0,0};
    if(d->push && !bt_circle_overlap(d->target_x-d->x,d->target_y-d->y,20*256)) {
        d->x+=(d->target_x-d->x)/12;d->y+=(d->target_y-d->y)/12;return;
    }
    d->push=0;
    int32_t attract=maximum(30,150+(150*g->run.stat100[BT_PICKUP_RANGE])/10000)*256;
    if(!d->attracted && bt_shape_circle(&rectangle,d->x-g->x,d->y-g->y,attract) &&
       (!consumable || g->run.hp<g->run.max_hp)) {
        d->attracted=1;if(!consumable)g->audio_events|=SOUND_MATERIAL_ATTRACT;
    }
    if(bt_shape_circle(&rectangle,d->x-g->x,d->y-g->y+24*256,32*256)) {
        if(consumable) {
            bt_player_heal(g,(d->kind==3?100:3)+g->run.stat100[BT_CONSUMABLE_HEAL]/100);
            if(d->kind) bt_queue_crate(g,d->kind);
        } else {
            g->run.gold+=d->value;g->materials_collected+=(uint32_t)d->value;
            g->audio_events|=SOUND_MATERIAL_PICKUP;
            bt_run_add_xp(&g->run,d->value);
        }
        d->active=0;return;
    }
    if(d->idle>0) {--d->idle;return;}
    if(d->attracted) {
        int32_t dx=g->x-d->x,dy=g->y-d->y,distance=bt_distance(dx,dy),step=d->speed*256/60;
        if(distance<=step) {d->x=g->x;d->y=g->y;}
        else {int32_t vx,vy;bt_unit_vector(dx,dy,step,&vx,&vy);d->x+=vx;d->y+=vy;}
        d->speed+=20;
    }
}
void bt_loot_tick(TatoGame *g)
{
    for(uint32_t i=0;i<TATO_DROPS;++i) tick_drop(g,&g->drops[i],0);
    for(uint32_t i=0;i<BT_CONSUMABLE_CAP;++i) tick_drop(g,&g->consumables[i],1);
    if(g->run.fault) bt_world_fault(g,64u);
}
void bt_player_heal(TatoGame *g,int32_t amount)
{if(amount>0 && g->run.hp>0) g->run.hp=minimum(g->run.max_hp,g->run.hp+amount);}
