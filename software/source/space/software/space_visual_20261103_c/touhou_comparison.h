#ifndef TOUHOU_COMPARISON_H
#define TOUHOU_COMPARISON_H
#include "touhou_game.h"
enum {TH_COMPARE_RESET=1,TH_COMPARE_EXIT=2,TH_COMPARE_ERROR=4};
typedef struct ThComparison {
    uint32_t enabled,software,scene,groups,cursor,previous_buttons,reload,hash;
    uint32_t valid[2],windows[2],fps100[2][2],checksums[2][2],strict[2][2],missed[2][2];
    uint32_t invalid_windows,desync,resets,reset_max_cycles;
} ThComparison;
void th_comparison_enter(ThComparison *p,uint32_t held_buttons);
unsigned th_comparison_step(ThComparison *p,ThGame *game,uint32_t buttons);
unsigned th_comparison_present(ThComparison *p,uint64_t cycles,uint32_t intervals,uint32_t missed,uint32_t work_over);
uint32_t th_comparison_hash(const ThGame *game);
#endif
