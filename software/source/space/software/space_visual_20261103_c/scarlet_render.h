#ifndef SCARLET_RENDER_H
#define SCARLET_RENDER_H
#include "scarlet_demo.h"
#include "touhou_comparison.h"
/* Full shared composition, rendered by the existing real SW/HW backends. */
void sc_render_demo(const ScDemo *demo,const ThGame *game,const ThComparison *view,
                    unsigned paused,unsigned volume,unsigned muted);
#endif
