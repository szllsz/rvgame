#include "fidelity_geometry.h"
#include "fidelity_rules.h"
extern const int16_t bt_sine_quarter[1025];
int32_t bt_sine(int32_t angle)
{
    uint32_t a=(uint32_t)angle&4095u,q=a>>10,t=a&1023u;
    int32_t value=bt_sine_quarter[(q&1u)?1024-t:t];
    return (q&2u)?-value:value;
}
void bt_rotate(int32_t x,int32_t y,int32_t angle,int32_t *rx,int32_t *ry)
{
    int32_t s=bt_sine(angle),c=bt_sine(angle+1024);
    *rx=(int32_t)(((int64_t)x*c-(int64_t)y*s)/32767);
    *ry=(int32_t)(((int64_t)x*s+(int64_t)y*c)/32767);
}
int32_t bt_radians_to_angle(int32_t radians1000)
{
    int64_t v=(int64_t)radians1000*651898647;
    return (int32_t)((v+(v<0?-500000000:500000000))/1000000000);
}
static int32_t absolute(int32_t value) {return value<0?-value:value;}
int bt_shape_circle(const BtShapeDef *s,int32_t x,int32_t y,int32_t radius)
{
    if(s->disabled) return 0;
    x=absolute(x-s->x);y=absolute(y-s->y);
    if(s->kind==1) {y-=s->b-s->a;if(y<0) y=0;radius+=s->a;}
    else if(s->kind==2) {x-=s->a;y-=s->b;if(x<0)x=0;if(y<0)y=0;}
    else radius+=s->a;
    /* x/y are nonnegative after the original shape projection. */
    if(radius>=0 && (x>=radius || y>=radius))return 0;
    if(radius>0 && radius<=32767)return x*x+y*y<radius*radius;
    return (int64_t)x*x+(int64_t)y*y<(int64_t)radius*radius;
}
int32_t bt_distance(int32_t dx,int32_t dy)
{
    /* Quarter-pixel squared coordinates stay within u32 for the 2048x1536 arena. */
    int64_t x=dx/64,y=dy/64;
    return (int32_t)bt_sqrt((uint32_t)(x*x+y*y))*64;
}
