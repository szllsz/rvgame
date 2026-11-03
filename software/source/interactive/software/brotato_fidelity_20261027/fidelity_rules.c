#include "fidelity_rules.h"

static double maximum(double a,double b) {return a>b?a:b;}
static double minimum(double a,double b) {return a<b?a:b;}
static double magnitude(double a) {return a<0?-a:a;}
static int32_t ceiling(double a) {int32_t i=(int32_t)a;return i+(a>i);}

int32_t bt_round(double value)
{return value<0?(int32_t)(value-0.5):(int32_t)(value+0.5);}

int32_t bt_weapon_damage(int32_t base,int32_t scaling_percent,int32_t scaling_stat100,int32_t percent_damage100)
{
    /* WeaponService: truncate scaled damage before percent damage and round. */
    int32_t scaled=(int32_t)maximum(1.0,base+(scaling_stat100/100.0)*(scaling_percent/100.0));
    return (int32_t)maximum(1,bt_round(scaled*(1+percent_damage100/10000.0)));
}
int32_t bt_cooldown(int32_t base,int32_t attack_speed100)
{
    double speed=attack_speed100/10000.0;
    if(base<2) base=2;
    return (int32_t)maximum(2,speed<0?base*(1+magnitude(speed)):base/(1+speed));
}
double bt_ranged_action_ticks(int32_t recoil_milliseconds,int32_t attack_speed100)
{
    double recoil=recoil_milliseconds/1000.0;
    if(attack_speed100>0) recoil/=1+attack_speed100/10000.0;
    return recoil*2*60;
}
double bt_melee_action_ticks(int32_t range,int32_t recoil_milliseconds,int32_t attack_speed100)
{
    double speed=attack_speed100/10000.0;
    double denominator=minimum(120,maximum(70,70*(1+speed/3)));
    double factor=maximum(0,range/denominator);
    double duration=maximum(.01,.2-speed/10)+factor*.15;
    double back=speed>0?.2/(1+speed*3):.2;
    double recoil=recoil_milliseconds/1000.0;
    if(speed>0) recoil/=1+speed;
    return (duration/2+back+recoil)*60;
}
double bt_random_cooldown(int32_t base,int32_t weapons,double draw)
{
    if(weapons>6) weapons=6;
    double spread=minimum(weapons*base/5.0,weapons*5.0);
    double low=maximum(1,base-spread),high=base+spread;
    return low+(high-low)*draw;
}
int32_t bt_critical_damage(int32_t damage,int32_t multiplier100)
{return bt_round(damage*(multiplier100/100.0));}
int32_t bt_player_damage(int32_t damage,int32_t armor)
{
    double coef=10.0/(10.0+(magnitude(armor)/1.5));
    if(armor<0) coef=(1.0-coef)+1.0;
    return (int32_t)maximum(1,bt_round(damage*coef));
}
int32_t bt_enemy_damage(int32_t damage,int32_t armor)
{return damage-armor>1?damage-armor:1;}
double bt_regen_seconds(int32_t regen)
{return regen<=0?99.0:5.0/(1.0+(magnitude(regen-1)/2.25));}
double bt_iframes_seconds(int32_t damage,int32_t max_hp)
{return minimum(.4,maximum(.2,((damage/(double)max_hp)*.4)/.15));}
int32_t bt_xp_needed(int32_t next_level)
{return (3+next_level)*(3+next_level);}
double bt_endless_factor(int32_t wave)
{
    int32_t endless=wave>20?wave-20:0;
    double multiplier=2.0+maximum(0,(wave-35)*.2);
    return maximum(0,((endless*(endless+1))/2)/100.0)*multiplier;
}
int32_t bt_shop_price(int32_t wave,int32_t base,int32_t weapon,int32_t weapon_price_pct,int32_t item_price_pct,int32_t affected)
{
    double value=base;
    if(weapon && affected) value=base*(1.0+weapon_price_pct/100.0);
    double factor=affected?1.0+item_price_pct/100.0:1.0;
    double endless=affected?bt_endless_factor(wave)/5.0:0;
    return (int32_t)maximum(1,((value+wave+value*wave*.1)*factor*(1+endless)));
}
int32_t bt_reroll_price(int32_t wave,int32_t count,int32_t modifier)
{
    /* sqrt is not needed in the common pre-endless case. */
    double root=1.0,value=1.0+bt_endless_factor(wave);
    if(wave>20) {
        root=value;
        for(uint32_t i=0;i<16;++i) root=(root+value/root)*.5;
    }
    int32_t delta=(int32_t)maximum(1,.4*wave*root);
    int32_t normal=(int32_t)(wave*.75)+delta+delta*count;
    return ceiling(normal*maximum(.1,1.0+modifier/100.0));
}
int32_t bt_recycle_price(int32_t wave,int32_t base,int32_t weapon,int32_t gain,int32_t item_price_pct)
{
    int32_t price=bt_shop_price(wave,base,weapon,0,item_price_pct,1);
    int32_t adjusted=bt_shop_price(wave,base,weapon,0,item_price_pct,wave<=20);
    double factor=minimum(1,maximum(.01,.25+gain/100.0));
    int32_t result=(int32_t)maximum(1,adjusted*factor);
    if(base==1) return 1;
    return result<price?result:price;
}
int32_t bt_pierce_damage(int32_t damage,int32_t reduction100)
{return (int32_t)maximum(1,damage-damage*(reduction100/100.0));}
static double danger_factor(int32_t danger)
{return danger<3?1.0:danger==3?1.12:danger==4?1.26:1.4;}
int32_t bt_enemy_health(int32_t base,int32_t increment100,int32_t wave,int32_t danger)
{
    int32_t health=(int32_t)(base+increment100/100.0*(wave-1));
    return bt_round(health*danger_factor(danger)*(1.0+bt_endless_factor(wave)*2.25));
}
int32_t bt_enemy_attack(int32_t base,int32_t increment100,int32_t wave,int32_t danger)
{return bt_round((base+increment100/100.0*(wave-1))*danger_factor(danger)*(1.0+bt_endless_factor(wave)));}
uint32_t bt_sqrt(uint32_t value)
{
    uint32_t result=0,bit=1u<<30;
    while(bit>value) bit>>=2;
    while(bit) {
        if(value>=result+bit) {value-=result+bit;result=(result>>1)+bit;}
        else result>>=1;
        bit>>=2;
    }
    return result;
}
static int32_t normalized_quotient(int32_t product,uint32_t divisor,uint32_t reciprocal)
{
    uint32_t numerator=(uint32_t)(product<0?-product:product);
    uint32_t quotient=(uint32_t)(((uint64_t)numerator*reciprocal)>>32);
    if(numerator-quotient*divisor>=divisor)++quotient;
    return product<0?-(int32_t)quotient:(int32_t)quotient;
}
void bt_unit_vector(int32_t dx,int32_t dy,int32_t speed,int32_t *vx,int32_t *vy)
{
    /* Preserve the Q4 norm, explicitly defining its original low-u32 squares. */
    int32_t x=dx/16,y=dy/16;
    uint32_t length=bt_sqrt((uint32_t)x*(uint32_t)x+(uint32_t)y*(uint32_t)y);
    if(!length) {*vx=0;*vy=0;return;}
    int64_t px=(int64_t)x*speed,py=(int64_t)y*speed;
    /* Excluding INT32_MIN keeps magnitude and signed negation defined. */
    if(px>=-INT32_MAX && px<=INT32_MAX && py>=-INT32_MAX && py<=INT32_MAX) {
        uint32_t reciprocal=UINT32_MAX/length;
        *vx=normalized_quotient((int32_t)px,length,reciprocal);
        *vy=normalized_quotient((int32_t)py,length,reciprocal);
    } else {
        *vx=(int32_t)(px/(int32_t)length);
        *vy=(int32_t)(py/(int32_t)length);
    }
}
uint32_t bt_shoulder_toggle(uint32_t enabled,uint32_t buttons,uint32_t previous)
{return enabled^(((buttons&0xc00u)==0xc00u && (previous&0xc00u)!=0xc00u)?1u:0u);}
