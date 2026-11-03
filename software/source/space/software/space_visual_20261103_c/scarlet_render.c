#include "scarlet_render.h"
#include "touhou_gfx.h"
#include "scarlet_assets.h"
#include "touhou_assets.h"
#include "native800_pause_assets.h"
#include "native800_space_ui.h"
/* Preserve the accepted640 layout, drawing directly into800 pixels. */
static int layout(int v){return v>=0?v*5/4:-((-v*5+3)/4);}
static unsigned extent(unsigned v){return (v*5+2)/4;}
static void layout_clip(int l,int t,int r,int b){th_clip(layout(l),layout(t),layout(r),layout(b));}
static void layout_fill(int x,int y,int w,int h,unsigned c){
    th_fill(layout(x),layout(y),layout(x+w)-layout(x),layout(y+h)-layout(y),c);
}
_Static_assert(SC_NATIVE800_DETAIL==1,"native detail atlas required");
static int mapped(int32_t value,int origin){
    int64_t n=(int64_t)value*3;
    return origin+(n>=0?(int)((n+4194304)/8388608):-(int)((-n+4194304)/8388608));
}
static void bitmap(const uint8_t *pixels,unsigned w,unsigned h,int x,int y){w=extent(w);h=extent(h);th_bitmap(pixels,w,w,h,layout(x),layout(y),4);}
static void text(unsigned id,int x,int y){const ScSprite *s=&sc_texts[id];th_bitmap(s->pixels,s->width,s->width,s->height,layout(x),layout(y),4);}
static void number(unsigned value,int x,int y,unsigned decimals){
    uint8_t digits[12];unsigned count=0;
    do{digits[count++]=(uint8_t)(value%10);value/=10;}while((value||count<=decimals)&&count<10);
    while(count){bitmap(sc_digits[digits[--count]],9,18,x,y);x+=9;if(decimals&&count==decimals){bitmap(sc_digits[10],9,18,x,y);x+=9;}}
}
static unsigned idle_frame(uint32_t tick){
    unsigned t=tick%50;
    return t<8?0:t<16?1:t<24?2:t<32?3:t<42?4:5;
}
void sc_render_demo(const ScDemo *demo,const ThGame *game,const ThComparison *view,
                    unsigned paused,unsigned volume,unsigned muted){
    layout_clip(0,0,640,480);layout_fill(0,0,640,480,0);
    /* Exact native source composition avoids paper behind the stage card.
     * The opaque dynamic field covers the omitted center rectangle. */
    th_bitmap(sc_static_frame,800,800,15,0,75,4);
    th_bitmap(sc_static_frame+435u*800u,800,800,15,0,510,4);
    th_bitmap(sc_static_frame+15u*800u,800,220,420,0,90,4);
    th_bitmap(sc_static_frame+15u*800u+580u,800,220,420,580,90,4);
    unsigned phase=(demo->tick/50)%16;
    if(view->scene==0)phase=0;else if(view->scene==1)phase&=12u;else if(view->scene==2)phase&=14u;
    bitmap(sc_background[phase],288,336,176,72);
    layout_clip(176,72,464,408);
    int bx=mapped(game->world.boss_x,176),by=mapped(game->world.boss_y,72);
    for(uint16_t slot=game->shot_head;slot!=TH_NONE;slot=game->shots[slot].next){
        const ThShot *shot=&game->shots[slot];
        const uint8_t *pixels=shot->kind==1?th_player_forward:shot->vx>0?th_player_seal_right:th_player_seal_left;
        bitmap(pixels,40,40,mapped(shot->x*8,176)-20,mapped(shot->y*8,72)-20);
    }
    for(uint16_t at=game->world.head;at!=TH_NONE;at=game->world.bullets[at].next){
        const ThBullet *b=&game->world.bullets[at];int x=mapped(b->x,176),y=mapped(b->y,72);
        if(b->kind==SC_BUBBLE)bitmap(sc_bubble,48,48,x-24,y-24);
        else if(b->graze_reset){bitmap(sc_spawn,24,24,x-12,y-12);}
        else bitmap(b->kind==SC_STILL?sc_still:sc_moving,12,12,x-6,y-6);
    }
    /* Opaque boss armor covers its emission origin; free-flight bullets stay
     * visible everywhere outside the sprite, without large additive halos. */
    th_bitmap(sc_remilia[idle_frame(demo->tick)],192,192,128,layout(bx)-96,layout(by)-64,4);
    /* Draw the invincible pilot last so the silhouette remains visible in a
     * dense barrage. This does not change trajectories or simulation state. */
    bitmap(sc_reimu[(demo->tick/7)%8],27,36,mapped(game->player.x,176)-13,mapped(game->player.y,72)-18);
    /* Original logic remains 384x448. Only measured/live fields are shown. */
    layout_clip(0,0,640,480);
    th_bitmap(sc_space_header,800,800,75,0,0,4);
    th_bitmap(sc_space_footer,800,800,75,0,525,4);
    th_bitmap(sc_space_modes[!!view->software],152,152,36,617,140,4);
    number(game->world.active,565,168,0);
    number(game->player.graze,565,207,0);
    number(view->groups,565,246,0);
    th_bitmap(sc_space_tiers[sc_density_tier(view->groups)],152,152,52,617,380,4);
    if(muted)text(SC_TEXT_MUTED,464,458);
    else number(volume,464,458,0);
    if(paused){
        unsigned choice=paused>>1;if(choice>2)choice=0;
        th_clip(0,0,800,600);
        th_bitmap(sc_pause_panels[choice],520,520,360,140,120,4);
        unsigned values[3]={volume,sc_density_tier(view->groups)+1,view->scene};
        for(unsigned i=0;i<3;i++)th_bitmap(sc_pause_digits[values[i]<9?values[i]:8],36,36,44,563,210+(int)i*60,4);
        th_bitmap(sc_pause_mutes[!!muted],108,108,26,508,151,4);
    }
    th_wait();
}
