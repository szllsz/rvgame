#include "touhou_collision.h"
#include "touhou_patterns.h"
static int circle(int32_t ax,int32_t ay,int32_t bx,int32_t by,int32_t radius){
    if((ax< -radius&&bx< -radius)||(ax>radius&&bx>radius)||
       (ay< -radius&&by< -radius)||(ay>radius&&by>radius))return 0;
    int64_t mx=(int64_t)bx-ax,my=(int64_t)by-ay;
    uint64_t length=(uint64_t)(mx*mx+my*my);int64_t dot=(int64_t)ax*mx+(int64_t)ay*my;
    uint32_t fraction=0;
    if(length&&dot<0){
        uint64_t remainder=(uint64_t)-dot;
        if(remainder>=length)fraction=65536;
        else for(unsigned i=0;i<16;i++){
            remainder*=2;fraction*=2;
            if(remainder>=length){remainder-=length;fraction++;}
        }
    }
    /* Keep Q21 at a circle endpoint; truncating to Q12 can turn a point
     * just outside the inclusive boundary into a premature fatal hit.
     * Ratio long division avoids overflowing dot*65536. No rotation is
     * needed for circles, so angle quantization cannot change their shape. */
    int64_t x=ax+mx*fraction/65536,y=ay+my*fraction/65536;
    return x*x+y*y<=(int64_t)radius*radius;
}
unsigned th_enemy_collision(void *context,ThBullet *b,int32_t old_x,int32_t old_y){
    if(b->kind<=2||b->kind==TH_CRYSTAL_RANDOM||b->kind==TH_RAIN_DROP)return th_player_collision(context,b,old_x,old_y);
    /* plainball: unrotated prototype25x25, fatal ellipse12x12. The
     * prototype size drives graze, never the atlas or collision-box size. */
    ThPlayer *p=context;
    if(b->graze_cooldown)b->graze_cooldown--;
    if(b->graze_count&&b->graze_reset&&!--b->graze_reset){b->graze_count--;b->graze_reset=90;}
    int32_t ax=p->x-p->vx-old_x,ay=p->y-p->vy-old_y,bx=p->x-b->x,by=p->y-b->y;
    p->hit_tests++;
    if(circle(ax,ay,bx,by,(b->kind==TH_RAIN_BIG?9:6)*TH_Q)){
        p->hits++;return 1;
    }
    if(b->graze_count>=3||b->graze_cooldown)return 0;
    static const int32_t graze[3]={419715,242323,187702},big_graze[3]={530902,306517,237427};unsigned n=b->graze_count;
    p->graze_tests++;
    int32_t diameter=b->kind==TH_RAIN_BIG?big_graze[n]:graze[n];
    if(th_segment_ellipse(ax,ay,bx,by,b->angle+0x40000000u,diameter,diameter)){
        p->points+=10+10*n;if(p->graze!=0xffffffffu)p->graze++;
        b->graze_count++;b->graze_cooldown=12;b->graze_reset=90;return 2;
    }
    return 0;
}
