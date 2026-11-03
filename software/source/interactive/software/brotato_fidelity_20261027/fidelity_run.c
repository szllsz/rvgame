#include "fidelity_run.h"
#include "fidelity_rules.h"
#include "fidelity_selection.h"
#include "fidelity_structure_data.h"

static int32_t maximum(int32_t a,int32_t b) {return a>b?a:b;}
static void zero(void *p,uint32_t n) {uint8_t *s=p;while(n--) *s++=0;}
uint32_t bt_random(BtRun *r)
{uint32_t v=r->random;v^=v<<13;v^=v>>17;v^=v<<5;return r->random=v;}
static uint32_t draw(BtRun *r,uint32_t count)
{return (uint32_t)(((uint64_t)bt_random(r)*count)>>32);}
void bt_run_refresh(BtRun *r)
{
    int32_t extra[BT_STAT_COUNT]={0};
    uint32_t sets[BT_SET_COUNT]={0};
    for(uint32_t i=0;i<r->weapon_count;++i) {
        const BtOwnedWeapon *w=&r->weapons[i];
        uint32_t mask=(uint32_t)bt_weapon_defs[w->kind][w->tier].set_mask;
        for(uint32_t s=0;s<BT_SET_COUNT;++s) if(mask&(1u<<s)) ++sets[s];
        if(w->kind==BT_HAND){static const int16_t harvesting[4]={3,6,12,25};extra[BT_HARVESTING]+=harvesting[w->tier];}
    }
    for(uint32_t s=0;s<BT_SET_COUNT;++s) if(sets[s]>=2)
        extra[bt_set_defs[s].stat]+=bt_set_defs[s].bonus[sets[s]>6?4:sets[s]-2];
    for(uint32_t i=0;i<BT_STAT_COUNT;++i)
        r->stat100[i]=(r->raw[i]+extra[i])*bt_role_defs[r->role].gain[i];
    int32_t hp=maximum(1,r->stat100[BT_MAX_HP]/100);
    if(hp>r->max_hp) r->hp+=hp-r->max_hp;
    r->max_hp=hp;
}
void bt_run_init(BtRun *r,uint32_t role,uint32_t weapon,uint32_t danger,uint32_t endless,uint32_t seed)
{
    zero(r,sizeof(*r));
    r->random=seed?seed:0x7461746fu;r->wave=1;r->role=role<BT_ROLE_COUNT?role:0;
    r->difficulty=danger<6?danger:5;r->endless=!!endless;
    for(uint32_t i=0;i<BT_STAT_COUNT;++i) r->raw[i]=bt_role_defs[r->role].effects[i];
    r->raw[BT_MAX_HP]+=10;
    if(weapon>=BT_WEAPON_KINDS || !(bt_starting_masks[r->role]&(1u<<weapon))) {
        weapon=0;while(!(bt_starting_masks[r->role]&(1u<<weapon))) ++weapon;
    }
    r->weapons[0].kind=(int16_t)weapon;r->weapon_count=1;
    int32_t extra=bt_role_defs[r->role].starting_extra;
    if(extra>=0) {r->weapons[1].kind=(int16_t)extra;r->weapon_count=2;}
    if(r->role==BT_PACIFIST)r->items[BT_LUMBERJACK_SHIRT]=1;
    bt_run_refresh(r);r->hp=r->max_hp;
}
void bt_run_add_xp(BtRun *r,int32_t value)
{
    if(value<=0) return;
    r->xp10000+=(int64_t)value*maximum(0,10000+r->stat100[BT_XP_GAIN]);
    int64_t needed=(int64_t)bt_xp_needed(r->level+1)*10000;
    while(r->xp10000>=needed) {
        r->xp10000-=needed;++r->level;++r->raw[BT_MAX_HP];
        if(r->pending_upgrades>=BT_UPGRADE_QUEUE) {r->fault|=1u;return;}
        r->upgrade_queue[(r->upgrade_head+r->pending_upgrades)%BT_UPGRADE_QUEUE]=(uint16_t)r->level;
        ++r->pending_upgrades;
        if((uint32_t)r->pending_upgrades>r->upgrade_peak) r->upgrade_peak=(uint32_t)r->pending_upgrades;
        bt_run_refresh(r);needed=(int64_t)bt_xp_needed(r->level+1)*10000;
    }
}
void bt_run_harvest(BtRun *r)
{bt_run_harvest_bonus(r,0);}
void bt_run_harvest_bonus(BtRun *r,int32_t bonus)
{
    int32_t amount=r->stat100[BT_HARVESTING]/100;
    int32_t reward=amount+bonus;
    if(reward>=0) {r->gold+=reward;bt_run_add_xp(r,reward);}
    else r->gold=maximum(0,r->gold+reward);
    if(amount>0) {
        if(r->wave<=20) r->raw[BT_HARVESTING]+=(amount*5+99)/100;
        else r->raw[BT_HARVESTING]-=(amount*20+99)/100;
        bt_run_refresh(r);
    }
}
int32_t bt_tier_from_draw(int32_t wave,int32_t luck100,int32_t random)
{
    static const int32_t min_wave[4]={0,0,2,6};
    static const double bonus[4]={0,.06,.02,.0023},caps[4]={1,.6,.25,.08};
    double luck=luck100/10000.0,roll=random/1000000.0;
    for(int32_t tier=3;tier>=1;--tier) {
        double chance=maximum(0,(wave-1)-min_wave[tier])*bonus[tier];
        chance=luck>=0?chance*(1+luck):chance/(1-luck);
        if(chance>caps[tier]) chance=caps[tier];
        if(roll<=chance) return tier;
    }
    return 0;
}
static int excluded(const BtOffer *offers,uint32_t weapon,uint32_t id,uint32_t tier)
{
    for(uint32_t i=0;i<4;++i) if(offers[i].active && offers[i].weapon==(int32_t)weapon &&
        offers[i].id==(int32_t)id && (!weapon || offers[i].tier==(int32_t)tier)) return 1;
    return 0;
}
static BtOffer choose(BtRun *r,uint32_t weapon,const BtOffer previous[4],int32_t fixed_tier)
{
    BtOffer result={0};
    result.weapon=(int16_t)weapon;result.price_wave=(int16_t)r->wave;result.active=1;
    int32_t tier=bt_tier_from_draw((int32_t)r->wave,r->stat100[BT_LUCK],(int32_t)draw(r,1000001));
    if(fixed_tier>=0) tier=fixed_tier;
    uint16_t normal[BT_ITEM_COUNT],wanted[BT_ITEM_COUNT],backup[BT_ITEM_COUNT];
    uint32_t n=0,w=0,b=0,set_mask=0;
    for(uint32_t i=0;i<r->weapon_count;++i) set_mask|=(uint32_t)bt_weapon_defs[r->weapons[i].kind][r->weapons[i].tier].set_mask;
    uint32_t roll=draw(r,1000000),want_tag=draw(r,1000000)<=50000;
    for(uint32_t i=0;i<(weapon?BT_WEAPON_KINDS:BT_ITEM_COUNT);++i) {
        if(weapon) {
            if(bt_role_defs[r->role].ban_melee && bt_weapon_defs[i][tier].type==0) continue;
            if(excluded(previous,1,i,(uint32_t)tier) || excluded(r->offers,1,i,(uint32_t)tier)) continue;
            backup[b++]=(uint16_t)i;
            uint32_t owned=0;
            for(uint32_t j=0;j<r->weapon_count;++j) if(r->weapons[j].kind==(int32_t)i) owned=1;
            double same=.2+maximum(0,6-(int32_t)r->wave)*.01;
            double family=.35+maximum(0,6-(int32_t)r->wave)*.04;
            if((roll/1000000.0<same && !owned) ||
               (roll/1000000.0>=same && roll/1000000.0<family && !(set_mask&(uint32_t)bt_weapon_defs[i][tier].set_mask))) continue;
            normal[n++]=(uint16_t)i;
        } else {
            const BtItemDef *item=&bt_item_defs[i];
            if(item->tier!=tier || excluded(previous,0,i,0) || excluded(r->offers,0,i,0)) continue;
            uint32_t count=r->items[i];
            for(uint32_t j=0;j<4;++j) if(r->offers[j].active && !r->offers[j].weapon && r->offers[j].id==(int32_t)i) ++count;
            if(item->max_nb>=0 && count>=(uint32_t)item->max_nb) continue;
            backup[b++]=(uint16_t)i;
            /* Of the chosen content, Brawler's original ranged_damage group bans Lens before wave20. */
            if(r->role==2 && r->wave<20 && i==5) continue;
            if(r->role==BT_PACIFIST && r->wave<20 && (i==34 || i==35 || i==36))continue;
            normal[n++]=(uint16_t)i;
            if(bt_item_tags[i]&bt_role_wanted[r->role]) wanted[w++]=(uint16_t)i;
        }
    }
    if(!weapon && want_tag && bt_role_wanted[r->role] && w) result.id=(int16_t)wanted[draw(r,w)];
    else if(n) result.id=(int16_t)normal[draw(r,n)];
    else if(b) result.id=(int16_t)backup[draw(r,b)];
    else {
        /* Original fallback reuses the tier pool after exclusions exhaust it. */
        for(uint32_t i=0;i<(weapon?BT_WEAPON_KINDS:BT_ITEM_COUNT);++i)
            if(weapon || bt_item_defs[i].tier==tier) backup[b++]=(uint16_t)i;
        if(!b) {result.active=0;r->fault|=2u;return result;}
        result.id=(int16_t)backup[draw(r,b)];
    }
    result.tier=(int16_t)tier;
    return result;
}
static void fill(BtRun *r,uint32_t keep_locked)
{
    BtOffer previous[4],locked[4];uint32_t count=0,weapon_count=0;
    for(uint32_t i=0;i<4;++i) {
        previous[i]=r->offers[i];
        if(keep_locked && r->offers[i].active && r->offers[i].locked) {
            locked[count++]=r->offers[i];weapon_count+=r->offers[i].weapon!=0;
        }
    }
    zero(r->offers,sizeof(r->offers));
    for(uint32_t i=0;i<count;++i) r->offers[i]=locked[i];
    uint32_t guaranteed=r->wave<=2?2:r->wave<=5?1:0;
    for(uint32_t i=count;i<4;++i) {
        uint32_t weapon=r->wave<=2?weapon_count<guaranteed:(draw(r,1000000)<=350000 || weapon_count<guaranteed);
        if(weapon) ++weapon_count;
        r->offers[i]=choose(r,weapon,previous,-1);
    }
}
void bt_shop_enter(BtRun *r)
{r->rerolls=0;r->free_reroll=0;fill(r,1);}
int32_t bt_offer_price(const BtRun *r,uint32_t slot)
{
    if(slot>=4 || !r->offers[slot].active) return 0;
    const BtOffer *o=&r->offers[slot];
    int32_t base=o->weapon?bt_weapon_defs[o->id][o->tier].value:bt_item_defs[o->id].value;
    return bt_shop_price(o->price_wave,base,o->weapon,0,0,1);
}
static void remove_weapon(BtRun *r,uint32_t index)
{for(uint32_t i=index;i+1<r->weapon_count;++i) r->weapons[i]=r->weapons[i+1];--r->weapon_count;}
int32_t bt_run_purchase(BtRun *r,uint32_t slot)
{
    if(slot>=4 || !r->offers[slot].active) return BT_SOLD;
    BtOffer *o=&r->offers[slot];int32_t price=bt_offer_price(r,slot),merge=-1;
    if(o->weapon && r->weapon_count==6) {
        if(o->tier<3) for(uint32_t i=0;i<6;++i)
            if(r->weapons[i].kind==o->id && r->weapons[i].tier==o->tier) {merge=(int32_t)i;break;}
        if(merge<0) return BT_FULL;
    }
    if(!o->weapon && bt_item_defs[o->id].max_nb>=0 && r->items[o->id]>=(uint32_t)bt_item_defs[o->id].max_nb) return BT_LIMIT;
    if(r->gold<price) return BT_NO_MONEY;
    r->gold-=price;
    if(o->weapon) {
        if(merge>=0) ++r->weapons[merge].tier;
        else {r->weapons[r->weapon_count].kind=o->id;r->weapons[r->weapon_count].tier=o->tier;++r->weapon_count;}
    } else {
        ++r->items[o->id];
        for(uint32_t i=0;i<BT_STAT_COUNT;++i) r->raw[i]+=bt_item_defs[o->id].effects[i];
    }
    o->active=o->locked=0;bt_run_refresh(r);
    uint32_t left=0;for(uint32_t i=0;i<4;++i) left+=r->offers[i].active!=0;
    r->free_reroll=!left;
    return BT_OK;
}
int32_t bt_run_reroll(BtRun *r)
{
    uint32_t locked=0;for(uint32_t i=0;i<4;++i) locked+=r->offers[i].active && r->offers[i].locked;
    if(locked==4) return BT_LOCKED_ALL;
    int32_t cost=r->free_reroll?0:bt_reroll_price((int32_t)r->wave,(int32_t)r->rerolls,0);
    if(r->gold<cost) return BT_NO_MONEY;
    r->gold-=cost;
    if(r->free_reroll) r->free_reroll=0;
    else ++r->rerolls;
    fill(r,1);return BT_OK;
}
int bt_run_can_combine(const BtRun *r,uint32_t slot)
{
    if(slot>=r->weapon_count || r->weapons[slot].tier>=3)return 0;
    for(uint32_t i=0;i<r->weapon_count;++i)
        if(i!=slot && r->weapons[i].kind==r->weapons[slot].kind && r->weapons[i].tier==r->weapons[slot].tier)return 1;
    return 0;
}
int32_t bt_run_recycle_value(const BtRun *r,uint32_t slot)
{
    if(slot>=r->weapon_count)return 0;
    BtOwnedWeapon w=r->weapons[slot];
    return bt_recycle_price((int32_t)r->wave,bt_weapon_defs[w.kind][w.tier].value,1,r->stat100[BT_RECYCLING_GAINS]/100,0);
}
int32_t bt_run_combine(BtRun *r,uint32_t slot)
{
    if(slot>=r->weapon_count || r->weapons[slot].tier==3) return BT_NO_PAIR;
    BtOwnedWeapon selected=r->weapons[slot];
    for(uint32_t i=0;i<r->weapon_count;++i) if(i!=slot && r->weapons[i].kind==selected.kind && r->weapons[i].tier==selected.tier) {
        uint32_t high=i>slot?i:slot,low=i<slot?i:slot;
        remove_weapon(r,high);remove_weapon(r,low);
        selected.tier++;r->weapons[r->weapon_count++]=selected;bt_run_refresh(r);return BT_OK;
    }
    return BT_NO_PAIR;
}
int32_t bt_run_recycle(BtRun *r,uint32_t slot)
{
    if(slot>=r->weapon_count) return BT_SOLD;
    int32_t value=bt_run_recycle_value(r,slot);
    remove_weapon(r,slot);r->gold+=value;bt_run_refresh(r);return BT_OK;
}
void bt_run_upgrade_offers(BtRun *r,uint8_t choices[4],uint8_t tiers[4])
{
    int32_t level=r->pending_upgrades?r->upgrade_queue[r->upgrade_head]:r->level;
    for(uint32_t i=0;i<4;++i) {
        uint32_t choice=0,tries=0;int32_t tier=0;
        for(;;) {
            tier=bt_tier_from_draw(level,r->stat100[BT_LUCK],(int32_t)draw(r,1000001));
            if(level==5) tier=1;
            else if(level==10 || level==15 || level==20) tier=2;
            else if(level%5==0) tier=3;
            choice=draw(r,BT_UPGRADE_COUNT);
            uint32_t repeat=0;
            for(uint32_t j=0;j<i;++j) if(choices[j]==choice) repeat=1;
            for(uint32_t j=0;j<r->previous_upgrade_count;++j) if(r->previous_upgrades[j]==choice) repeat=1;
            if(!repeat || tries++>=50) break;
        }
        choices[i]=(uint8_t)choice;
        tiers[i]=(uint8_t)tier;
    }
    for(uint32_t i=0;i<4;++i) r->previous_upgrades[i]=choices[i];
    r->previous_upgrade_count=4;
}
void bt_run_apply_upgrade(BtRun *r,uint32_t choice,uint32_t tier)
{
    if(!r->pending_upgrades || choice>=BT_UPGRADE_COUNT || tier>3) return;
    r->raw[bt_upgrade_defs[choice].stat]+=bt_upgrade_defs[choice].value[tier];
    --r->pending_upgrades;r->upgrade_head=(r->upgrade_head+1)%BT_UPGRADE_QUEUE;bt_run_refresh(r);
}
int32_t bt_run_crate_item(BtRun *r,uint32_t legendary)
{
    BtOffer old[4],none[4]={0};
    for(uint32_t i=0;i<4;++i) {old[i]=r->offers[i];if(!r->offers[i].locked)r->offers[i].active=0;}
    BtOffer offer=choose(r,0,none,legendary?3:-1);
    for(uint32_t i=0;i<4;++i) r->offers[i]=old[i];
    return offer.active?offer.id:-1;
}
void bt_run_add_item(BtRun *r,uint32_t item)
{
    if(item>=BT_ITEM_COUNT) {r->fault|=4u;return;}
    ++r->items[item];
    for(uint32_t i=0;i<BT_STAT_COUNT;++i) r->raw[i]+=bt_item_defs[item].effects[i];
    bt_run_refresh(r);
}
