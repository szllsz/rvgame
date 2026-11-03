/* Taisei1.4.6 random.c xoshiro256+ / splitmix64 adapted under MIT;
 * source COPYRIGHT/COPYING.upstream retained. Fixed geometry is project code. */
#include "touhou_math.h"
#include "atan_table.h"
uint32_t th_angle(int32_t x,int32_t y){
    if(!x&&!y)return 0;
    /* Extra fractional bits prevent tiny vectors losing CORDIC precision. */
    int64_t vx=(int64_t)x*1073741824,vy=(int64_t)y*1073741824;uint32_t angle=0;
    if(vx<0){vx=-vx;vy=-vy;angle=0x80000000u;}
    for(unsigned i=0;i<32&&vy;i++){
        int64_t old=vx;
        if(vy>0){vx+=vy>>i;vy-=old>>i;angle+=th_atan_turn[i];}
        else{vx-=vy>>i;vy+=old>>i;angle-=th_atan_turn[i];}
    }
    return angle;
}
uint64_t th_root64(uint64_t value){
    uint64_t out=0,bit=UINT64_C(1)<<62;
    while(bit>value)bit>>=2;
    while(bit){if(value>=out+bit){value-=out+bit;out=(out>>1)+bit;}else out>>=1;bit>>=2;}
    return out;
}
static int32_t scaled(int32_t value,int32_t magnitude,uint64_t norm){
    int64_t v=(int64_t)value*magnitude;
    uint64_t a=v<0?(uint64_t)-v:(uint64_t)v;
    int32_t n=(int32_t)((a+norm/2)/norm);return v<0?-n:n;
}
void th_normalize(int32_t x,int32_t y,int32_t magnitude,int32_t *out_x,int32_t *out_y){
    if(!x&&!y){*out_x=*out_y=0;return;}
    uint64_t ax=x<0?(uint64_t)-(int64_t)x:(uint64_t)x;
    uint64_t ay=y<0?(uint64_t)-(int64_t)y:(uint64_t)y,largest=ax>ay?ax:ay;
    /* Preserve the ratio of subpixel vectors before taking an integer root.
     * A ceil(sqrt(1+1)) alone would badly distort normalize(1,1). */
    while(largest<(UINT64_C(1)<<30)){x+=x;y+=y;largest<<=1;}
    uint64_t square=(uint64_t)((int64_t)x*x)+(uint64_t)((int64_t)y*y);
    uint64_t norm=th_root64(square);if(norm*norm<square)norm++;
    *out_x=scaled(x,magnitude,norm);*out_y=scaled(y,magnitude,norm);
}
static uint64_t splitmix(uint64_t *state){
    uint64_t z=(*state+=UINT64_C(0x9e3779b97f4a7c15));
    z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
    z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);return z^(z>>31);
}
void th_rng_init(ThRng *rng,uint64_t seed){
    for(unsigned i=0;i<4;i++)rng->state[i]=splitmix(&seed);
    rng->draws=0;
}
uint64_t th_rng_next(ThRng *rng){
    uint64_t *s=rng->state,result=s[0]+s[3],t=s[1]<<17;
    s[2]^=s[0];s[3]^=s[1];s[1]^=s[2];s[0]^=s[3];s[2]^=t;
    s[3]=(s[3]<<45)|(s[3]>>19);rng->draws++;return result;
}
int32_t th_random_signed_q30(uint64_t value){
    uint64_t v=value&UINT64_C(0x7fffffffffffffff);
    int32_t magnitude=(int32_t)((v+(UINT64_C(1)<<32))>>33);
    return value>>63?-magnitude:magnitude;
}
uint32_t th_random_angle(uint64_t value){return (uint32_t)(value>>32);}
uint32_t th_random_float_angle(uint64_t value){
    union{float f;uint32_t u;} a;
    a.f=(float)((double)(value>>11)*0x1.0p-53*6.283185307179586);
    unsigned exponent=(a.u>>23)&255u;
    if(!exponent)return 0;
    unsigned shift=150u-exponent;if(shift>=64)return 0;
    uint64_t n=(uint64_t)((a.u&0x7fffffu)|0x800000u)*UINT64_C(683565276);
    return (uint32_t)((n+(UINT64_C(1)<<(shift-1)))>>shift);
}
