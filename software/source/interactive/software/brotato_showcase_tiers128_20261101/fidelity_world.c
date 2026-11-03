#include "fidelity_world.h"
#include "fidelity_rules.h"
#include "fidelity_selection.h"
void bt_zero(void *memory,uint32_t count) {uint8_t *p=memory;while(count--) *p++=0;}
uint32_t bt_draw(TatoGame *g,uint32_t count)
{return (uint32_t)(((uint64_t)bt_random(&g->run)*count)>>32);}
int32_t bt_rand_range(TatoGame *g,int32_t low,int32_t high)
{return low+(int32_t)bt_draw(g,(uint32_t)(high-low));}
int bt_chance(TatoGame *g,int32_t millionths)
{return millionths>0 && (int32_t)bt_draw(g,1000001)<=millionths;}
void bt_world_fault(TatoGame *g,uint32_t bit)
{g->fault|=bit;g->mode=TATO_FAULT;}
void bt_effect(TatoGame *g,int32_t x,int32_t y,int32_t kind)
{
    for(uint32_t i=0;i<TATO_EFFECTS;++i) if(!g->effects[i].life) {
        g->effects[i].x=x;g->effects[i].y=y;g->effects[i].life=12;g->effects[i].kind=kind;return;
    }
}
void tato_init(TatoGame *g)
{bt_zero(g,sizeof(*g));bt_run_init(&g->run,0,0,0,0,0x7461746fu);g->mode=TATO_TITLE;g->setup_field=BT_SETUP_HOME;g->event_base=-1;}
void bt_begin_wave(TatoGame *g)
{
    g->population_removals_at_wave_start=g->original_population_removals;
    bt_run_refresh(&g->run);g->run.hp=g->run.max_hp;
    g->x=TATO_WORLD_WIDTH*128;g->y=TATO_WORLD_HEIGHT*128;g->invulnerable=g->regen_ticks=g->lifesteal_ticks=0;
    bt_structures_init(g);bt_waves_init(g);bt_player_weapons_init(g);
    if(g->fault) return;
    g->mode=TATO_PLAY;g->selected=0;g->inventory_slot=BT_POPUP_NONE;g->audio_events|=SOUND_WAVE;
}
static void next_reward(TatoGame *g)
{
    g->selected=0;g->inventory_slot=BT_POPUP_NONE;g->shop_column=0;
    if(g->crate_count) {
        g->mode=TATO_CRATE;g->crate_item=bt_run_crate_item(&g->run,g->crate_queue[g->crate_head]==3);
        if(g->crate_item<0) bt_world_fault(g,1024u);
    } else if(g->run.pending_upgrades) {
        g->mode=TATO_UPGRADE;bt_run_upgrade_offers(&g->run,g->upgrade_choices,g->upgrade_tiers);
    } else {g->mode=TATO_SHOP;bt_shop_enter(&g->run);}
}
void bt_queue_crate(TatoGame *g,int32_t kind)
{
    if(g->crate_count>=BT_CONSUMABLE_CAP) {++g->consumable_overflows;bt_world_fault(g,32u);return;}
    g->crate_queue[(g->crate_head+g->crate_count)%BT_CONSUMABLE_CAP]=(uint8_t)kind;++g->crate_count;
    if(kind==3) ++g->legendary_crates;else ++g->crates;
}
int32_t bt_pacifist_bonus(const TatoGame *g)
{
    if(g->run.role!=BT_PACIFIST)return 0;
    uint32_t count=g->original_population_removals-g->population_removals_at_wave_start;
    for(uint32_t i=0;i<TATO_ENEMIES;++i)if(g->enemies[i].hp>0 && !g->enemies[i].birth && g->enemies[i].type!=2)++count;
    int32_t bonus=(int32_t)((count*65u+50u)/100u);
    return g->horde?bonus/2:bonus;
}
static void settle_wave(TatoGame *g)
{
    for(uint32_t i=0;i<TATO_DROPS;++i) if(g->drops[i].active) {g->run.bonus_gold+=g->drops[i].value;g->drops[i].active=0;}
    for(uint32_t i=0;i<BT_CONSUMABLE_CAP;++i) if(g->consumables[i].active && g->consumables[i].kind) {
        bt_queue_crate(g,g->consumables[i].kind);if(g->fault)return;
        g->consumables[i].active=0;
    }
    int32_t bonus=bt_pacifist_bonus(g);g->pacifist_bonus_total+=(uint32_t)bonus;
    bt_run_harvest_bonus(&g->run,bonus);g->upgrade_rerolls=0;g->run.previous_upgrade_count=0;
    if(g->run.fault) {bt_world_fault(g,64u);return;}
    if(g->wave==20 && !g->run.endless) {g->mode=TATO_WIN;return;}
    next_reward(g);
}
void bt_wave_end(TatoGame *g)
{
    if(g->showcase){
        /* Repeat the original bounded workload, retaining the selected tier.
         * Only LT/RT chooses another tier; combat cannot accumulate forever. */
        bt_showcase_restart(g);
        return;
    }
    for(uint32_t i=0;i<TATO_DROPS;++i)if(g->drops[i].active){
        /* Original cleanup attracts uncollected materials to the bonus bag,
         * not to player gold/XP. Keep the full value until settlement. */
        g->mode=TATO_COLLECT;g->collect_ticks=120;return;
    }
    settle_wave(g);
}
static void collect_tick(TatoGame *g)
{
    if(!g->collect_ticks){settle_wave(g);return;}
    for(uint32_t i=0;i<TATO_DROPS;++i){BtDrop *d=&g->drops[i];if(!d->active)continue;
        /* Destination is the visible bag counter (66,458), mapped back into
         * the existing world projection. Only this presentation phase moves it. */
        int32_t x=(66-48)*64*256/17,y=(458-40)*64*256/17;
        d->x+=(x-d->x)/(int32_t)g->collect_ticks;
        d->y+=(y-d->y)/(int32_t)g->collect_ticks;
    }
    if(!--g->collect_ticks)settle_wave(g);
}
int tato_price(const TatoGame *g,uint32_t choice)
{return bt_offer_price(&g->run,choice);}
const char *tato_offer_name(uint32_t offer)
{return offer<BT_ITEM_COUNT?bt_item_defs[offer].name:"";}
static void setup(TatoGame *g,uint32_t pressed)
{
    uint32_t field=g->setup_field,role=g->setup_role,weapon=g->setup_weapon,difficulty=g->setup_difficulty,endless=g->setup_endless;
    if(field==BT_SETUP_HOME){
        if(pressed&(16u|32u)){g->selected^=1u;g->audio_events|=SOUND_UI_FOCUS;}
        if(pressed&(2u|8u)){
            if(g->selected){bt_showcase_start(g);g->audio_events|=SOUND_UI_PRESS;}
            else {g->setup_field=0;g->audio_events|=SOUND_UI_PRESS;}
        }
        return;
    }
    if(pressed&1u){g->setup_field=field?field-1:BT_SETUP_HOME;g->selected=0;g->audio_events|=SOUND_UI_PRESS;return;}
    int32_t direction=(pressed&128u)?1:(pressed&64u)?-1:0;
    if(direction) {
        if(g->setup_field==0) {
            g->setup_role=(g->setup_role+BT_ROLE_COUNT+(uint32_t)direction)%BT_ROLE_COUNT;
            while(!(bt_starting_masks[g->setup_role]&(1u<<g->setup_weapon))) g->setup_weapon=(g->setup_weapon+1)%BT_WEAPON_KINDS;
        } else if(g->setup_field==1) {
            do g->setup_weapon=(g->setup_weapon+BT_WEAPON_KINDS+(uint32_t)direction)%BT_WEAPON_KINDS;
            while(!(bt_starting_masks[g->setup_role]&(1u<<g->setup_weapon)));
        } else if(g->setup_field==2) g->setup_difficulty=(g->setup_difficulty+6+(uint32_t)direction)%6;
        else if(g->setup_field==3) g->setup_endless=direction>0;
    }
    if(field!=g->setup_field || role!=g->setup_role || weapon!=g->setup_weapon || difficulty!=g->setup_difficulty || endless!=g->setup_endless)
        g->audio_events|=SOUND_UI_FOCUS;
    if((pressed&(2u|8u)) && g->setup_field<4) {++g->setup_field;g->audio_events|=SOUND_UI_PRESS;return;}
    if((pressed&(2u|8u)) && g->setup_field==4) {
        g->audio_events|=SOUND_UI_PRESS;
        uint32_t seed=g->run.random^g->frame;
        bt_run_init(&g->run,g->setup_role,g->setup_weapon,g->setup_difficulty,g->setup_endless,seed);
        bt_begin_wave(g);
    }
}
static void shop(TatoGame *g,uint32_t pressed)
{
    if(g->inventory_slot==BT_POPUP_ITEM) {
        if(pressed&(3u|256u)){g->inventory_slot=BT_POPUP_NONE;g->audio_events|=SOUND_UI_PRESS;}return;
    }
    if(g->inventory_slot>=BT_POPUP_COMBINE){
        if(g->selected<6 || g->selected-6>=g->run.weapon_count){g->inventory_slot=BT_POPUP_NONE;return;}
        if(pressed&1u){g->inventory_slot=BT_POPUP_NONE;g->audio_events|=SOUND_UI_PRESS;return;}
        uint32_t first=bt_run_can_combine(&g->run,g->selected-6)?BT_POPUP_COMBINE:BT_POPUP_RECYCLE;
        uint32_t old=g->inventory_slot;
        if(pressed&(16u|64u))g->inventory_slot=g->inventory_slot>first?g->inventory_slot-1:BT_POPUP_CANCEL;
        if(pressed&(32u|128u))g->inventory_slot=g->inventory_slot<BT_POPUP_CANCEL?g->inventory_slot+1:first;
        if(old!=g->inventory_slot)g->audio_events|=SOUND_UI_FOCUS;
        if(pressed&2u){
            int32_t result=-1;
            if(g->inventory_slot==BT_POPUP_COMBINE)result=bt_run_combine(&g->run,g->selected-6);
            else if(g->inventory_slot==BT_POPUP_RECYCLE)result=bt_run_recycle(&g->run,g->selected-6);
            g->inventory_slot=BT_POPUP_NONE;g->audio_events|=SOUND_UI_PRESS;
            if(result>=0){g->message=(uint32_t)result;g->audio_events|=result==BT_OK?SOUND_BUY:SOUND_FAIL;}
            if(g->selected>=6+g->run.weapon_count)g->selected=g->run.weapon_count?5+g->run.weapon_count:6;
        }
        return; /* Start/X/Y never bypass a weapon confirmation modal. */
    }
    uint32_t old=g->selected;
    if(g->selected<4)g->shop_column=g->selected;
    if(pressed&16u) g->selected=g->selected==5?6:g->selected>=6?g->shop_column:4;
    if(pressed&32u) g->selected=g->selected==4?g->shop_column:g->selected<4?6:5;
    if(g->selected<4){
        if(pressed&64u)g->selected=(g->selected+3)%4;
        if(pressed&128u)g->selected=(g->selected+1)%4;
        g->shop_column=g->selected;
    }else if(g->selected>=6 && g->run.weapon_count){
        if(pressed&64u)g->selected=6+(g->selected-6+g->run.weapon_count-1)%g->run.weapon_count;
        if(pressed&128u)g->selected=6+(g->selected-5)%g->run.weapon_count;
    }
    if(old!=g->selected)g->audio_events|=SOUND_UI_FOCUS;
    if((pressed&512u) && g->selected<4 && g->run.offers[g->selected].active) {g->run.offers[g->selected].locked^=1;g->audio_events|=SOUND_UI_PRESS;}
    int32_t result=-1;
    if(pressed&256u){g->audio_events|=SOUND_UI_PRESS;result=bt_run_reroll(&g->run);}
    else if(pressed&2u) {
        g->audio_events|=SOUND_UI_PRESS;
        if(g->selected<4) result=bt_run_purchase(&g->run,g->selected);
        else if(g->selected==4) result=bt_run_reroll(&g->run);
        else if(g->selected==5) {++g->run.wave;bt_begin_wave(g);return;}
        else if(g->selected-6<g->run.weapon_count){
            g->inventory_slot=BT_POPUP_CANCEL;g->audio_events|=SOUND_UI_PRESS;return;
        }
    }
    if(pressed&8u) {++g->run.wave;bt_begin_wave(g);return;}
    if(result>=0) {g->message=(uint32_t)result;g->audio_events|=result==BT_OK?SOUND_BUY:SOUND_FAIL;}
    if(g->selected>=6+g->run.weapon_count) g->selected=g->run.weapon_count?5+g->run.weapon_count:6;
}
void tato_tick(TatoGame *g,uint32_t buttons,uint32_t pressed)
{
    ++g->frame;g->audio_events=0;
    if((pressed&4u) || ((pressed&1u) && g->mode==TATO_TITLE && g->setup_field==BT_SETUP_HOME)) {g->mode=TATO_EXIT;return;}
    g->test_invincible=g->showcase?1:bt_shoulder_toggle(g->test_invincible,buttons,g->previous);g->previous=buttons;
    if(g->mode==TATO_TITLE) {setup(g,pressed);return;}
    if(g->mode==TATO_DEAD || g->mode==TATO_WIN) {if(pressed&2u){tato_init(g);g->previous=buttons;}return;}
    if(g->mode==TATO_FAULT || g->mode==TATO_EXIT) return;
    if(g->mode==TATO_COLLECT){collect_tick(g);return;}
    if(g->mode==TATO_PAUSE) {if(pressed&11u){g->mode=TATO_PLAY;g->audio_events|=SOUND_UI_PRESS;}return;}
    if(g->mode==TATO_SHOP) {shop(g,pressed);return;}
    if(g->mode==TATO_UPGRADE) {
        uint32_t old=g->selected;
        if(pressed&64u) g->selected=g->selected>0 && g->selected<4?g->selected-1:3;
        if(pressed&128u) g->selected=g->selected<3?g->selected+1:0;
        if((pressed&16u) && g->selected==4) g->selected=0;
        if(pressed&32u) g->selected=4;
        if(old!=g->selected)g->audio_events|=SOUND_UI_FOCUS;
        if(pressed&2u) {
            g->audio_events|=SOUND_UI_PRESS;
            if(g->selected<4) {
                bt_run_apply_upgrade(&g->run,g->upgrade_choices[g->selected],g->upgrade_tiers[g->selected]);next_reward(g);g->audio_events|=SOUND_LEVEL;
            } else {
                int32_t price=bt_reroll_price((int32_t)g->wave,(int32_t)g->upgrade_rerolls,0);
                if(g->run.gold>=price) {g->run.gold-=price;++g->upgrade_rerolls;bt_run_upgrade_offers(&g->run,g->upgrade_choices,g->upgrade_tiers);g->audio_events|=SOUND_BUY;}
                else g->audio_events|=SOUND_FAIL;
            }
        }
        return;
    }
    if(g->mode==TATO_CRATE) {
        if(pressed&3u) {
            g->audio_events|=SOUND_UI_PRESS;
            if(pressed&2u) bt_run_add_item(&g->run,(uint32_t)g->crate_item);
            else g->run.gold+=bt_recycle_price((int32_t)g->wave,bt_item_defs[g->crate_item].value,0,g->run.stat100[BT_RECYCLING_GAINS]/100,0);
            if(g->crate_queue[g->crate_head]==3) --g->legendary_crates;else --g->crates;
            g->crate_head=(g->crate_head+1)%BT_CONSUMABLE_CAP;--g->crate_count;
            next_reward(g);
        }
        return;
    }
    if(pressed&8u) {g->mode=TATO_PAUSE;return;}
    ++g->ticks;++g->logic_total;if(g->test_invincible) ++g->god_ticks;else ++g->normal_ticks;
    bt_waves_tick(g);if(g->fault)return;
    if(g->showcase){
        /* Clockwise perimeter route, using the ordinary player movement path.
         * Each stage starts at the same corner; only showcase owns direction. */
        int32_t left=100*256,top=100*256,right=(TATO_WORLD_WIDTH-100)*256,bottom=(TATO_WORLD_HEIGHT-100)*256;
        buttons=g->y<=top && g->x<right?128u:g->x>=right && g->y<bottom?32u:g->y>=bottom && g->x>left?64u:16u;
    }
    bt_player_tick(g,buttons);if(g->fault || g->mode==TATO_DEAD)return;
    bt_enemies_tick(g);if(g->fault || g->mode==TATO_DEAD)return;
    bt_structures_tick(g);if(g->fault || g->mode==TATO_DEAD)return;
    bt_mob_projectiles_tick(g);if(g->fault || g->mode==TATO_DEAD)return;
    bt_loot_tick(g);
    for(uint32_t i=0;i<TATO_EFFECTS;++i) if(g->effects[i].life) --g->effects[i].life;
    if(g->mode!=TATO_PLAY) return;
    if(g->remaining) --g->remaining;
    if(!g->remaining) bt_wave_end(g);
}
