#ifndef GFX_MOTION_H
#define GFX_MOTION_H
#include <stdint.h>
/* Same trajectories as the original uint32 phase*speed modulo equations.
 * Large phases use the original equations so uint32 wrap semantics stay exact.
 */
static inline int gfx_phase_offsets(uint32_t phase,uint32_t dx[4],uint32_t dy[4]) {
    if(phase>1073741416u) return 0;
    uint32_t px=phase%656u,py=phase%368u,x=0,y=0;
    for(unsigned i=0;i<4;i++) {
        x+=px;y+=py;if(x>=656u)x-=656u;if(y>=368u)y-=368u;
        dx[i]=x;dy[i]=y;
    }
    return 1;
}
static inline int gfx_position_x(uint32_t base,uint32_t offset,int shift) {
    uint32_t x=base+offset+(uint32_t)(shift+656);
    if(x>=656u)x-=656u;
    if(x>=656u)x-=656u;
    if(x>=656u)x-=656u;
    return (int)x-8;
}
static inline int gfx_position_y(uint32_t base,uint32_t offset) {
    uint32_t y=base+offset;if(y>=368u)y-=368u;return 56+(int)y;
}
#endif
