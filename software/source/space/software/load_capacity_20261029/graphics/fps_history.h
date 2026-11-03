/* Recent full-screen measurements, counted from hardware-completed presents.
 * DRAW(28) is framebuffer_engine.swap_count, incremented on frame_presented.
 * This is the same event used by the external FPS OSD, not render submissions.
 */
#ifndef FPS_HISTORY_H
#define FPS_HISTORY_H
#include <stdint.h>
typedef struct {
    uint32_t start_ticks,start_completed,fps100[2],frames[2],ticks[2],draw100[2];
    uint64_t draw_sum;
    unsigned draw_samples;
    unsigned initialized,active,mode,valid,count,opacity,paused;
    int shift;
} FullFpsHistory;
static FullFpsHistory full_fps;
static void full_fps_observe(uint32_t now,uint32_t completed,unsigned pair,unsigned sw,
                             unsigned count,unsigned opacity,unsigned paused,int shift)
{
    if(!full_fps.initialized || full_fps.count!=count || full_fps.opacity!=opacity ||
       full_fps.paused!=paused || full_fps.shift!=shift){
        full_fps.valid=full_fps.active=0;full_fps.initialized=1;
        full_fps.count=count;full_fps.opacity=opacity;full_fps.paused=paused;full_fps.shift=shift;
        for(unsigned i=0;i<2;i++)full_fps.fps100[i]=full_fps.frames[i]=full_fps.ticks[i]=full_fps.draw100[i]=0;
    }
    if(pair || paused){full_fps.active=0;return;}
    if(!full_fps.active || full_fps.mode!=sw){
        full_fps.start_ticks=now;full_fps.start_completed=completed;
        full_fps.draw_sum=0;full_fps.draw_samples=0;
        full_fps.active=1;full_fps.mode=sw;return;
    }
    uint32_t elapsed=now-full_fps.start_ticks;
    if(elapsed>=210000000u){
        uint32_t frames=completed-full_fps.start_completed;
        full_fps.frames[sw]=frames;full_fps.ticks[sw]=elapsed;
        full_fps.fps100[sw]=(uint32_t)((uint64_t)frames*105000000u*100u/elapsed);
        full_fps.valid|=1u<<sw;
        full_fps.draw100[sw]=full_fps.draw_samples?(uint32_t)(full_fps.draw_sum/full_fps.draw_samples/1050u):0;
        full_fps.draw_sum=0;full_fps.draw_samples=0;
        full_fps.start_ticks=now;full_fps.start_completed=completed;
    }
}
static void full_fps_reference(void){full_fps.valid=full_fps.active=0;}
#endif
