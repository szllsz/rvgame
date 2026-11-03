/* Source rules: player.c, plrmodes/reimu.c, projectile.c, util/geometry.c.
 * Taisei 1.4.6 MIT; see COPYING.upstream. */
#include "touhou_player.h"
static int32_t abs32(int32_t n){return n<0?-n:n;}
static int32_t clamp(int32_t v,int32_t l,int32_t r){return v<l?l:v>r?r:v;}
static uint32_t root(uint32_t n){
    uint32_t out=0,bit=1u<<30;
    while(bit>n)bit>>=2;
    while(bit){if(n>=out+bit){n-=out+bit;out=(out>>1)+bit;}else out>>=1;bit>>=2;}
    return out;
}
void th_player_init(ThPlayer *p){
    *p=(ThPlayer){0};p->x=240*TH_Q;p->y=496*TH_Q;
}
void th_player_move(ThPlayer *p,int32_t qx,int32_t qy,unsigned focus){
    qx=clamp(qx,-32767,32767);qy=clamp(qy,-32767,32767);
    uint32_t norm=(uint32_t)(qx*qx)+(uint32_t)(qy*qy);
    if(norm>1073676289u){
        uint32_t n=root(norm);if((uint64_t)n*n<norm)n++;
        qx=(int32_t)((int64_t)qx*32767/n);qy=(int32_t)((int64_t)qy*32767/n);
    }
    p->focused=!!focus;int32_t speed=focus?5*TH_Q/2:45*TH_Q/8;
    int32_t x=clamp(p->x+(int32_t)((int64_t)qx*speed/32767),16*TH_Q,464*TH_Q);
    int32_t y=clamp(p->y+(int32_t)((int64_t)qy*speed/32767),16*TH_Q,544*TH_Q);
    p->vx=x-p->x;p->vy=y-p->y;p->x=x;p->y=y;
}
int th_segment_ellipse(int32_t ax,int32_t ay,int32_t bx,int32_t by,uint32_t angle,int32_t dx,int32_t dy){
    /* Q12 geometry only after source AABB rejection. Scene-relative differences
     * remain far below int64 limits; Q21 motion state is never overwritten. */
    ax/=512;ay/=512;bx/=512;by/=512;
    int32_t radius=(dx>dy?dx:dy)/2;
    if((ax< -radius&&bx< -radius)||(ax>radius&&bx>radius)||
       (ay< -radius&&by< -radius)||(ay>radius&&by>radius))return 0;
    int32_t c=th_cos(angle),s=th_sin(angle);
    int32_t ar=th_mul_q30(ax,c)+th_mul_q30(ay,s);
    int32_t ai=(int32_t)((int64_t)(th_mul_q30(ay,c)-th_mul_q30(ax,s))*dx/dy);
    int32_t br=th_mul_q30(bx,c)+th_mul_q30(by,s);
    int32_t bi=(int32_t)((int64_t)(th_mul_q30(by,c)-th_mul_q30(bx,s))*dx/dy);
    int32_t mx=br-ar,my=bi-ai;
    int64_t length=(int64_t)mx*mx+(int64_t)my*my;
    int64_t dot=(int64_t)ar*mx+(int64_t)ai*my;
    int32_t fraction=(!length||dot>=0)?0:-dot>=length?65536:(int32_t)(-dot*65536/length);
    int32_t px=ar+(int32_t)((int64_t)mx*fraction/65536);
    int32_t py=ai+(int32_t)((int64_t)my*fraction/65536);
    return (int64_t)px*px+(int64_t)py*py<=(int64_t)(dx/2)*(dx/2);
}
unsigned th_player_collision(void *context,ThBullet *b,int32_t old_x,int32_t old_y){
    ThPlayer *p=context;
    if(b->graze_cooldown)b->graze_cooldown--;
    if(b->graze_count&&b->graze_reset&&!--b->graze_reset){b->graze_count--;b->graze_reset=90;}
    int32_t ax=p->x-p->vx-old_x,ay=p->y-p->vy-old_y;
    int32_t bx=p->x-b->x,by=p->y-b->y;
    p->hit_tests++;
    if(th_segment_ellipse(ax,ay,bx,by,b->angle+0x40000000u,6*4096,12*4096)){
        p->hits++;return 1; /* source projectile is fatal even if player immune */
    }
    if(b->graze_count>=3||b->graze_cooldown)return 0;
    /* sqrt(420*prototype_size/(2*graze_counter+1)), nearest Q12. */
    static const int32_t sizes[3][2]={{265451,375405},{153258,216740},{118713,167886}};
    unsigned n=b->graze_count;p->graze_tests++;
    if(abs32(ax)>sizes[n][1]/2*512&&abs32(bx)>sizes[n][1]/2*512&&((ax<0)==(bx<0)))return 0;
    if(th_segment_ellipse(ax,ay,bx,by,b->angle+0x40000000u,sizes[n][0],sizes[n][1])){
        p->points+=10+10*n;if(p->graze!=0xffffffffu)p->graze++;
        b->graze_count++;b->graze_cooldown=12;b->graze_reset=90;return 2;
    }
    return 0;
}
