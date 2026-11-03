#ifndef SCARLET_PRESENTATION_H
#define SCARLET_PRESENTATION_H
#include "scarlet_render.h"
extern ScDemo sc_demo_state;
extern unsigned sc_demo_paused;
extern unsigned sc_demo_pause_choice;
void sc_demo_move(ThComparison *p,ThGame *g,int32_t x,int32_t y,unsigned focus);
#endif
