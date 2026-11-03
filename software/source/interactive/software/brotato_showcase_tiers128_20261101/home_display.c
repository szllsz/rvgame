#include "brotato_fidelity_display.h"
#include "fidelity_visual_data.h"
#include "fidelity_held_assets.h"
#include "fidelity_attack_assets.h"
#include "fidelity_visibility_assets.h"
#include "fidelity_wrench_assets.h"
#include "fidelity_pacifist_assets.h"
#include "fidelity_geometry.h"
#include "fidelity_rules.h"
#include "fidelity_ground_regions.h"
#include "fidelity_ui_data.h"
#include "fidelity_home_assets.h"
#include "home_bounce.h"
#include "subtitle_font.h"
#include "focus_font.h"
#include "enemy_projectile_visuals.h"
#include "home_background.h"
#ifndef TATO_HOST
#include "native800_home_detail.h"
#include "egg_brand.h"
#include "native800_visual_data_v3.h"
#include "native800_effect_detail.h"
#undef bt_wrench_sweep_sprites
#define bt_wrench_sweep_sprites bt_world_wrench_sprites
#define bt_home_logo home_detail_logo
#define bt_home_bounce home_detail_bounce
#define WORLD_ROLE_SPRITES bt_world_role_sprites
enum {FONT_KEY=227};
#else
#define WORLD_ROLE_SPRITES bt_role_sprites
enum {FONT_KEY=0};
#endif
#include "fidelity_selection.h"
#include "arena_audio.h"
#include "game_gfx.h"
#include "native800_frame.h"
#include "platform_input.h"
#include "fidelity_world.h"
_Static_assert(sizeof(WORLD_ROLE_SPRITES)/sizeof(WORLD_ROLE_SPRITES[0]) == BT_ROLE_COUNT, "Every selectable role needs a battle sprite");
static int camera_active;
static int32_t camera_x,camera_y;
#ifndef TATO_HOST
#include "camera_world_tables.h"
#endif
#undef UI_SHOWCASE_BUILD
#undef UI_SHOWCASE_RULES
#define UI_SHOWCASE_BUILD "D5 双Boss 六手掌 小怪混战"
#define UI_SHOWCASE_RULES "LT/RT 切换阶段 双Boss加小怪"
#define DRAW(o) (*(volatile uint32_t *)(uintptr_t)(0x10000200u+(o)))
static int focus_x,focus_y,focus_width,focus_height;
static unsigned bitmap_opacity=4;
#ifdef TATO_HOST
void tato_host_rect(int x,int y,int w,int h,int color);
void tato_host_bitmap(const uint8_t *pixels,int w,int h,int x,int y,int flip);
void tato_host_bitmap_key(const uint8_t *pixels,int w,int h,int x,int y,int flip,int key);
void tato_host_bitmap_half(const uint8_t *pixels,int w,int h,int x,int y,int key);
void tato_host_missing_glyph(unsigned code);
#endif
static void rect(int x,int y,int w,int h,int color)
{
    if(x<0) {w+=x;x=0;}if(y<0) {h+=y;y=0;}
    if(x+w>640)w=640-x;
    if(y+h>480)h=480-y;
    if(w<=0 || h<=0)return;
#ifdef TATO_HOST
    tato_host_rect(x,y,w,h,color);
#else
    native800_fill(x,y,w,h,(unsigned)color);
#endif
}
static void bitmap_key(const BtSprite *s,int x,int y,int flip,int key)
{
#ifdef TATO_HOST
    x+=s->x;y+=s->y;
    tato_host_bitmap_key(s->pixels,s->width,s->height,x,y,flip,key);
#else
    x=native800_layout_coordinate(x)+s->x;y=native800_layout_coordinate(y)+s->y;
    native800_bitmap_physical(s->pixels,(unsigned)s->width,(unsigned)s->width,(unsigned)s->height,
                             x,y,(unsigned)flip,0,key<0?0:1u|((unsigned)key<<8),bitmap_opacity);
#endif
}
static void bitmap(const BtSprite *s,int x,int y,int flip){bitmap_key(s,x,y,flip,0);}
static void ui_bitmap(const BtSprite *s,int x,int y){bitmap_key(s,x,y,0,BT_UI_TRANSPARENT_KEY);}
static void bitmap_half(const BtSprite *s,int x,int y,int key)
{
#ifdef TATO_HOST
    tato_host_bitmap_half(s->pixels,s->width,s->height,x+s->x,y+s->y,key);
#else
    bitmap_opacity=2;bitmap_key(s,x,y,0,key);game_wait();bitmap_opacity=4;
#endif
}
static unsigned held_direction(int32_t dx,int32_t dy)
{
    int32_t x=dx<0?-dx:dx,y=dy<0?-dy:dy;
    unsigned n=y*1000<x*199?0:y*1000<x*668?1:y*668<x*1000?2:y*199<x*1000?3:4;
    if(!x && !y)return 0;
    if(dx<0)n=8-n;
    return dy<0?(16-n)&15:n;
}
static unsigned next_character(const char **cursor)
{
    const unsigned char *s=(const unsigned char *)*cursor;unsigned c=*s++;
    if(c>=0xe0 && c<0xf0 && s[0] && s[1] && (s[0]&0xc0)==0x80 && (s[1]&0xc0)==0x80)
        {c=((c&15)<<12)|((s[0]&63)<<6)|(s[1]&63);s+=2;}
    else if(c>=0xc0 && c<0xe0 && s[0] && (s[0]&0xc0)==0x80) {c=((c&31)<<6)|(s[0]&63);++s;}
    else if(c>=128)c='?';
    *cursor=(const char *)s;return c;
}
static void character_color(int x,int y,unsigned code,unsigned color)
{
    if(x>=focus_x && y>=focus_y && x<focus_x+focus_width && y<focus_y+focus_height){
        unsigned i=0;while(i<FOCUS_GLYPHS && focus_codes[i]!=code)++i;
        if(i<FOCUS_GLYPHS){bitmap_key(&focus_glyphs[i][color<3?color:0],x,y,0,227);return;}
    }
    if(code>127){unsigned i=0;while(i<SUBTITLE_COUNT && subtitle_codes[i]!=code)i++;
        if(i<SUBTITLE_COUNT){if(color==3)bitmap_half(&subtitle_16[i][0],x,y,FONT_KEY);else bitmap_key(&subtitle_16[i][color],x,y,0,FONT_KEY);return;}}
    unsigned left=0,right=BT_UI_GLYPH_COUNT;
    while(left<right){unsigned mid=(left+right)/2;if(bt_ui_codes[mid]<code)left=mid+1;else right=mid;}
    if(left<BT_UI_GLYPH_COUNT && bt_ui_codes[left]==code){
        if(color==3)bitmap_half(&bt_ui_glyphs[left],x,y,FONT_KEY);
        else bitmap_key(color?&bt_ui_tinted_glyphs[color-1][left]:&bt_ui_glyphs[left],x,y,0,FONT_KEY);
    }
    else {
#ifdef TATO_HOST
        tato_host_missing_glyph(code);
#endif
        rect(x+1,y+1,9,1,224);rect(x+1,y+11,9,1,224);rect(x+1,y+1,1,11,224);rect(x+9,y+1,1,11,224);}
}
static void character(int x,int y,unsigned code){character_color(x,y,code,0);}
static int text_width(const char *s)
{int width=0;while(*s){unsigned c=next_character(&s);width+=c<128?6:16;}return width;}
static void text_limit_color(int x,int y,const char *s,int width,unsigned color)
{
    int end=x+width;if(end>638)end=638;
    while(*s){unsigned c=next_character(&s);int advance=c<128?6:16;if(x+advance>end)break;
        if(c!=' ')character_color(x,y,c,color);
        x+=advance;}
}
static void text_limit(int x,int y,const char *s,int width){text_limit_color(x,y,s,width,0);}
static void text(int x,int y,const char *s) {text_limit(x,y,s,638-x);}
static void text_color(int x,int y,const char *s,unsigned color){text_limit_color(x,y,s,638-x,color);}
static void centered(int x,int y,int width,const char *s)
{int length=text_width(s);text_limit(x+(length<width?(width-length)/2:0),y,s,width);}
static void heading(int center,int y,const char *s)
{
    int width=0;const char *measure=s;while(*measure){unsigned c=next_character(&measure);width+=c<128?12:24;}
    int x=center-width/2;while(*s){unsigned c=next_character(&s),i=0;
        while(i<BT_UI_BIG_COUNT && bt_ui_big_codes[i]!=c)++i;
        unsigned n=0;while(n<SUBTITLE_COUNT && subtitle_codes[n]!=c)n++;
        if(c>127 && n<SUBTITLE_COUNT){
#ifdef TATO_HOST
            bitmap(&subtitle_24[n][0],x,y,0);
#else
            bitmap_key(&subtitle_24[n],x,y,0,FONT_KEY);
#endif
        }
        else if(i<BT_UI_BIG_COUNT)bitmap_key(&bt_ui_big_glyphs[i],x,y,0,FONT_KEY);
        else character(x,y,c);
        x+=c<128?12:24;}
}
static void wrapped(int x,int y,int width,int lines,const char *s)
{
    int start=x,end=x+width;
    while(*s && lines>0){unsigned c=next_character(&s);int advance=c<128?6:16;
        if(c=='\n' || x+advance>end){x=start;y+=17;if(--lines==0)break;if(c=='\n')continue;}
        if(c!=' ')character(x,y,c);
        x+=advance;}
}
static void role_effect(int x,int y,int width,const char *s)
{
    int start=x,end=x+width;unsigned color=0;
    while(*s){
        if(s[0]=='[' && s[1] && s[2]==']' && (s[1]=='g' || s[1]=='r' || s[1]=='w')){
            color=s[1]=='g'?1u:s[1]=='r'?2u:0u;s+=3;continue;}
        unsigned code=next_character(&s),left=0,right=BT_UI_ROLE_GLYPH_COUNT;int advance=code<128?8:16;
        if(x+advance>end){x=start;y+=21;}
        while(left<right){unsigned mid=(left+right)/2;if(bt_ui_role_codes[mid]<code)left=mid+1;else right=mid;}
        if(code!=' '){
            if(code>127)character_color(x,y,code,color);
            else if(left<BT_UI_ROLE_GLYPH_COUNT && bt_ui_role_codes[left]==code)bitmap_key(&bt_ui_role_glyphs[color][left],x,y,0,FONT_KEY);
            else {
#ifdef TATO_HOST
                tato_host_missing_glyph(code);
#endif
                character_color(x,y,code,color);
            }
        }
        x+=advance;
    }
}
static void number_color(int x,int y,int32_t value,unsigned color)
{
    char out[12],reverse[11];uint32_t size=0,n=value<0?(uint32_t)(-(int64_t)value):(uint32_t)value,pos=0;
    if(value<0)out[pos++]='-';
    do {reverse[size++]=(char)('0'+n%10);n/=10;}while(n);
    while(size)out[pos++]=reverse[--size];
    out[pos]=0;text_color(x,y,out,color);
}
static void number(int x,int y,int32_t value){number_color(x,y,value,0);}
static void signed_number(int x,int y,int32_t value)
{unsigned color=value>0?1u:value<0?2u:0u;if(value>0){text_color(x,y,"+",color);x+=6;}number_color(x,y,value,color);}
static void hundredths_color(int x,int y,int32_t value,unsigned color)
{
    char out[16],reverse[11];uint32_t n=value<0?(uint32_t)(-(int64_t)value):(uint32_t)value,pos=0,size=0,whole=n/100;
    if(value<0)out[pos++]='-';
    do{reverse[size++]=(char)('0'+whole%10);whole/=10;}while(whole);
    while(size)out[pos++]=reverse[--size];
    if(n%100){out[pos++]='.';out[pos++]=(char)('0'+n%100/10);out[pos++]=(char)('0'+n%10);}
    out[pos]=0;text_color(x,y,out,color);
}
static int32_t displayed_cooldown_ms(const BtWeaponStats *s)
{
    /* Original WeaponStats adds the shooting action duration to the cooldown.
     * This is display only; attack scheduling remains in fidelity_player.c. */
    int64_t ticks=(int64_t)s->cooldown*BT_TICK_ONE+s->action_ticks;
    return (int32_t)((ticks*1000+30*BT_TICK_ONE)/(60*BT_TICK_ONE));
}
static void cooldown_text(int x,int y,const BtWeaponStats *s,unsigned color)
{
    int32_t value=displayed_cooldown_ms(s);
    if(value>=200)hundredths_color(x,y,(value+5)/10,color);
    else {char out[6]={'0','.',(char)('0'+value/100),(char)('0'+value/10%10),(char)('0'+value%10),0};text_color(x,y,out,color);}
}
static unsigned sign_color(int32_t value){return value>0?1u:value<0?2u:0u;}
static unsigned difference_color(int32_t value,int32_t base){return value>base?1u:value<base?2u:0u;}
static int screen_x(int32_t x)
{return camera_active?320+(int)(((int64_t)x-camera_x)*119/(320*256)):48+(int)((int64_t)x*17/(64*256));}
static int screen_y(int32_t y)
{return camera_active?244+(int)(((int64_t)y-camera_y)*119/(320*256)):40+(int)((int64_t)y*17/(64*256));}
static int world_size(int n)
{return camera_active?(n>=0?(n*7+2)/5:(n*7-2)/5):n;}
static void world_rect(int x,int y,int dx,int dy,int w,int h,int color)
{rect(x+world_size(dx),y+world_size(dy),world_size(w),world_size(h),color);}
static void camera_select(const TatoGame *g)
{
#ifndef TATO_HOST
    camera_active=!g->showcase;
    /* Original single-player camera snaps to its target and clamps to the
     * zone plus 96-unit side/bottom and 192-unit top margins. HUD is fixed.
     * Our 7/5 view uses the existing 640-layout viewport before native800. */
    const int32_t hx=320*320*256/119,hy=205*320*256/119;
    int32_t left=-96*256+hx,right=(TATO_WORLD_WIDTH+96)*256-hx;
    int32_t top=-192*256+hy,bottom=(TATO_WORLD_HEIGHT+96)*256-hy;
    camera_x=g->x<left?left:g->x>right?right:g->x;
    camera_y=g->y<top?top:g->y>bottom?bottom:g->y;
#else
    (void)g;camera_active=0;
#endif
}
static void panel(int x,int y,int width,int height)
{rect(x-2,y-2,width+4,height+4,0);rect(x,y,width,height,73);}
static int tier_color(unsigned tier)
{/* Original white/blue/purple/red, quantized by RGB332's high bits. */
    static const uint8_t colors[4]={255,87,171,228};return colors[tier<4?tier:3];}
static void card_color(int x,int y,int width,int height,int color,int selected)
{
    rect(x-2,y-2,width+4,height+4,selected?255:0);rect(x,y,width,height,color);
    rect(x+3,y+3,width-6,height-6,selected?182:0);
    if(selected){focus_x=x+3;focus_y=y+3;focus_width=width-6;focus_height=height-6;}
}
static void card(int x,int y,int width,int height,unsigned tier,int selected)
{card_color(x,y,width,height,tier_color(tier),selected);}
static void button(int x,int y,int width,int height,int selected)
{card_color(x,y,width,height,73,selected);}
#ifndef TATO_HOST
static uint32_t initial_clears;
#endif
void tato_graphics_init(void) {
    game_viewport(640,480);
#ifndef TATO_HOST
    initial_clears=2;
#endif
}
static void background(void)
{
#ifndef TATO_HOST
    if(camera_active){
        int left=screen_x(0),top=screen_y(0);
        int right=screen_x(TATO_WORLD_WIDTH*256),bottom=screen_y(TATO_WORLD_HEIGHT*256);
        rect(0,0,640,480,BT_GROUND_OUTER);rect(left,top,right-left,bottom-top,BT_GROUND_BASE);
        int px=native800_layout_coordinate(left)-84,py=native800_layout_coordinate(top)-70;
        for(unsigned i=0;i<CAMERA_GROUND_PATCHES;++i){
            const BtSprite *p=&camera_ground_patches[i];
            native800_bitmap_physical(p->pixels,(unsigned)p->width,(unsigned)p->width,(unsigned)p->height,
                                     px+p->x,py+p->y,0,0,0,4);
        }
        return;
    }
#endif
    /* Five fast fills plus sparse opaque patches reproduce the full ROM image. */
    rect(0,0,640,BT_GROUND_TOP,BT_GROUND_OUTER);
    rect(0,BT_GROUND_BOTTOM,640,480-BT_GROUND_BOTTOM,BT_GROUND_OUTER);
    rect(0,BT_GROUND_TOP,BT_GROUND_LEFT,BT_GROUND_BOTTOM-BT_GROUND_TOP,BT_GROUND_OUTER);
    rect(BT_GROUND_RIGHT,BT_GROUND_TOP,640-BT_GROUND_RIGHT,BT_GROUND_BOTTOM-BT_GROUND_TOP,BT_GROUND_OUTER);
    rect(BT_GROUND_LEFT,BT_GROUND_TOP,BT_GROUND_RIGHT-BT_GROUND_LEFT,BT_GROUND_BOTTOM-BT_GROUND_TOP,BT_GROUND_BASE);
#ifdef TATO_HOST
    for(uint32_t i=0;i<BT_GROUND_PATCH_COUNT;++i) {
        const BtGroundPatch *p=&bt_ground_patches[i];const uint8_t *pixels=bt_ground.pixels+p->y*640u+p->x;
        for(uint32_t row=0;row<p->height;++row)tato_host_bitmap(pixels+row*640u,p->width,1,p->x,p->y+(int)row,0);
    }
#else
    for(unsigned i=0;i<NATIVE800_GROUND_PATCHES;++i){
        const BtSprite *p=&native800_ground_patches[i];
        native800_bitmap_physical(p->pixels,(unsigned)p->width,(unsigned)p->width,(unsigned)p->height,p->x,p->y,0,0,0,4);
    }
#endif
}
static void world(const TatoGame *g)
{
    for(uint32_t i=0;i<BT_STRUCTURE_CAP;++i){const BtStructure *s=&g->structures[i];if(!s->state)continue;
        int x=screen_x(s->x),y=screen_y(s->y);
        if(s->state==1){world_rect(x,y,-7,-1,15,3,31);world_rect(x,y,-1,-7,3,15,31);}
        else {
            if(s->kind<5){unsigned age=s->age<0?18u:(unsigned)s->age/BT_TICK_ONE;if(age>18)age=18;
                bitmap_key(&bt_attack_turrets[s->kind][age],x,y,0,BT_ATTACK_KEY);}
            else bitmap(s->kind==BT_GARDEN?&bt_garden_sprite:&bt_structure_sprites[s->kind],x,y,0);
            if(s->kind==BT_MINE && s->shots)world_rect(x,y,-4,2,9,2,224);
        }
    }
    for(uint32_t i=0;i<TATO_DROPS;++i)if(g->drops[i].active)bitmap_key(&bt_green_material,screen_x(g->drops[i].x),screen_y(g->drops[i].y),0,BT_VISIBILITY_KEY);
    for(uint32_t i=0;i<BT_CONSUMABLE_CAP;++i)if(g->consumables[i].active) {
        const BtDrop *d=&g->consumables[i];bitmap(&bt_pickup_sprites[d->kind==3?3:d->kind==1?2:1],screen_x(d->x),screen_y(d->y),0);
    }
    for(uint32_t i=0;i<TATO_ENEMIES;++i) {
        const TatoEntity *e=&g->enemies[i];int x=screen_x(e->x),y=screen_y(e->y);
        if(e->birth) {world_rect(x,y,-8,-2,17,3,224);world_rect(x,y,-2,-8,3,17,224);continue;}
        if(e->hp<=0)continue;
        bitmap(&bt_enemy_sprites[e->kind],x,y,e->x>g->x);
        if(e->burn_remaining && (g->frame&4))world_rect(x,y,-3,-7,7,3,252);
        if(bt_enemy_defs[e->kind].boss || e->hp<e->max_hp) {
            int origin=bt_enemy_sprites[e->kind].y;
#ifndef TATO_HOST
            origin=origin>=0?(origin*4+2)/5:(origin*4-2)/5;
#endif
            int width=world_size(bt_enemy_defs[e->kind].boss?70:24),top=y+origin-world_size(5);
            rect(x-width/2,top,width,world_size(3),64);rect(x-width/2,top,(int)((int64_t)e->hp*width/e->max_hp),world_size(3),e->flash?255:28);
        }
    }
    if(!g->invulnerable || (g->frame&4))bitmap(&WORLD_ROLE_SPRITES[g->run.role],screen_x(g->x),screen_y(g->y),0);
    for(uint32_t i=0;i<g->run.weapon_count;++i) {
        int32_t x,y,dx,dy;bt_player_weapon_pose(g,i,&x,&y,&dx,&dy);
        unsigned direction=held_direction(dx,dy);
        const BtSprite *sprite=g->run.weapons[i].kind==BT_WRENCH?
            &bt_wrench_sweep_sprites[g->weapon_dx[i]<0][direction]:&bt_held_sprites[g->run.weapons[i].kind][direction];
        bitmap_key(sprite,screen_x(x),screen_y(y),0,BT_HELD_KEY);
    }
    for(uint32_t i=0;i<TATO_BULLETS;++i)if(g->bullets[i].active){const BtProjectile *p=&g->bullets[i];int x=screen_x(p->x),y=screen_y(p->y);
        if(p->kind<=-100){int kind=-p->kind-100;
            bitmap_half(&bt_attack_explosions[kind==BT_MINE],x,y,BT_ATTACK_KEY);
        }else if(p->kind<0){unsigned kind=(unsigned)(-p->kind-1),age=g->structure_projectile_age[i];
            unsigned frame=kind==1?(age/3<8?age/3:7):kind==3?0:age>=2;
            bitmap_key(&bt_attack_projectiles[kind][frame][held_direction(p->vx,p->vy)],x,y,0,BT_ATTACK_KEY);
        }
        else bitmap(p->kind==BT_TASER?&bt_taser_sprite:&bt_projectile_sprites[0],x,y,0);
    }
    for(uint32_t i=0;i<BT_MOB_SHOTS;++i)if(g->mob_shots[i].active) {
        const BtMobShot *b=&g->mob_shots[i];
        if(b->kind>=3){unsigned frame=0;static const int32_t times[7]={0,28800,30600,32400,34200,36000,37800};
            while(frame<6 && b->age>=times[frame+1])++frame;
            unsigned direction=b->rotate?held_direction(b->dir_x,b->dir_y):0;
            int32_t ox=0,oy=0;
            if(b->kind==4){ox=165*256;if(b->rotate){ox=(int32_t)((int64_t)165*256*b->dir_x/32767);oy=(int32_t)((int64_t)165*256*b->dir_y/32767);}}
            bitmap_key(&bt_red_slash[frame][direction],screen_x(b->x+ox),screen_y(b->y+oy),0,BT_VISIBILITY_KEY);
        }else bitmap_key(&bt_red_enemy_projectiles[b->kind],screen_x(b->x),screen_y(b->y),b->vx<0,BT_VISIBILITY_KEY);
    }
    for(uint32_t i=0;i<TATO_EFFECTS;++i)if(g->effects[i].life) {
        int x=screen_x(g->effects[i].x),y=screen_y(g->effects[i].y);world_rect(x,y,-2,-2,5,5,252);
    }
}
static void hud(const TatoGame *g)
{
    rect(0,0,640,39,0);rect(0,449,640,31,0);
    rect(8,7,160,19,64);rect(8,7,(int)((int64_t)g->run.hp*160/g->run.max_hp),19,196);
    text(14,10,UI_HEALTH);number(66,10,g->run.hp);text(92,10,"/");number(104,10,g->run.max_hp);
    bitmap(&bt_pickup_sprites[0],194,16,0);number(208,10,g->run.gold);
    text(282,3,g->showcase?"阶段":UI_WAVE);number(316,3,g->showcase?(g->run.wave<10?1:g->run.wave<20?2:3):(int32_t)g->wave);
    if(g->showcase){text(334,3,"/3");text(282,21,g->run.wave<10?"60帧极限":g->run.wave<20?"开始掉帧":"约30帧");}
    else text(282,21,g->run.endless?UI_ENDLESS:UI_CLASSIC);
    if(g->showcase){text(438,448,"LT/RT");text(510,448,"切换");}
    else {text(438,448,UI_TIME);number(510,448,(int32_t)((g->remaining+59)/60));}
    if(g->test_invincible){rect(390,8,146,21,64);text(398,12,g->showcase?"无敌 自动巡航":UI_TEST_GOD " TEST GOD");}
    text(8,448,UI_BAG);number(78,448,g->run.bonus_gold);text(152,448,UI_LEVEL);number(192,448,g->run.level+1);
    text(242,448,UI_DIFFICULTY);number(314,448,(int32_t)g->run.difficulty);
    for(uint32_t i=0;i<g->run.weapon_count;++i)bitmap(&bt_weapon_sprites[g->run.weapons[i].kind],408+(int)i*38,465,0);
    int64_t needed=(int64_t)bt_xp_needed(g->run.level+1)*10000;
    text(8,464,"经验");number(40,466,(int32_t)(g->run.xp10000/10000));text(78,466,"/");number(90,466,(int32_t)(needed/10000));
    rect(146,469,236,8,36);if(needed>0)rect(146,469,(int)(g->run.xp10000*236/needed),8,60);
}
static void stats(const TatoGame *g,int x,int y)
{
    static const int types[16]={BT_MAX_HP,BT_HP_REGENERATION,BT_LIFESTEAL,BT_PERCENT_DAMAGE,
        BT_MELEE_DAMAGE,BT_RANGED_DAMAGE,BT_ELEMENTAL_DAMAGE,BT_ATTACK_SPEED,BT_CRIT_CHANCE,
        BT_ENGINEERING,BT_RANGE,BT_ARMOR,BT_DODGE,BT_SPEED,BT_LUCK,BT_HARVESTING};
    panel(x,y,136,360);centered(x,y+9,136,UI_STATS);
    for(int i=0;i<16;++i){int top=y+34+i*19,stat=types[i];int32_t value=g->run.stat100[stat];
        ui_bitmap(&bt_ui_stat_icons[stat],x+12,top+6);
        text_limit_color(x+23,top,bt_ui_stat_names[stat],82,sign_color(value));number_color(x+105,top,value/100,sign_color(value));}
}
static const char *weapon_info(unsigned kind)
{
    switch(kind){case BT_HAND:return UI_HAND_INFO;case BT_TASER:return UI_TASER_INFO;
        case BT_PRUNER:return UI_PRUNER_INFO;case BT_WRENCH:return UI_WRENCH_INFO;
        case BT_SCREWDRIVER:return UI_SCREWDRIVER_INFO;default:return "";}
}
static void weapon_details(int x,int y,int width,const BtWeaponDef *w)
{
    static const int32_t zero[BT_STAT_COUNT]={0};BtWeaponStats base;bt_prepare_weapon(&base,w,zero,0);
    text(x,y,UI_DAMAGE);number(x+56,y,w->damage);
    text(x,y+18,UI_COOLDOWN);cooldown_text(x+56,y+18,&base,0);text(x+92,y+18,UI_SECONDS);
    text(x,y+36,UI_RANGE);number(x+56,y+36,w->range);
    wrapped(x,y+57,width,2,weapon_info((unsigned)w->kind));
}
static void title(const TatoGame *g)
{
    static const char *const steps[5]={UI_CHARACTER,UI_WEAPON,UI_DIFFICULTY,UI_ENDLESS,UI_CONFIRM};
    if(g->setup_field==BT_SETUP_HOME){
        ui_bitmap(&bt_home_logo,(640-bt_home_logo.width*4/5)/2,18);
        ui_bitmap(&bt_home_bounce[g->frame%90u],320,0);
        button(170,326,300,36,g->selected==0);centered(170,337,300,UI_START_GAME);
        button(170,374,300,36,g->selected==1);centered(170,385,300,UI_SHOWCASE);
        centered(0,424,640,g->selected?"双Boss混战 / 无敌自动巡航":UI_SOLO);centered(0,445,640,UI_HOME_HINT);
        centered(0,464,640,arena_audio_muted()?"START 选项  " UI_SFX_MUTED:"START 选项  " UI_SFX_ON);
        return;
    }
    panel(18,16,604,42);
#ifndef TATO_HOST
    ui_bitmap(&egg_title,268,20);
#else
    heading(320,23,UI_TITLE);
#endif
    text(465,32,UI_SOLO);
    heading(320,70,steps[g->setup_field<5?g->setup_field:0]);
    if(g->setup_field==0){
        panel(18,109,604,243);heading(320,118,bt_ui_role_names[g->setup_role]);
        rect(38,152,148,160,0);rect(44,158,136,148,73);
        static const uint8_t shadow_width[9]={32,48,60,70,74,70,60,48,32};
        for(unsigned row=0;row<9;++row)rect(112-shadow_width[row]/2,278+(int)row,shadow_width[row],1,0);
        ui_bitmap(&bt_ui_role_large_icons[g->setup_role],112,231);
        int extra=bt_role_defs[g->setup_role].starting_extra;
        if(extra>=0 || g->setup_role==BT_PACIFIST){card(40,320,144,28,0,0);text(51,327,"初始");
            if(extra>=0)ui_bitmap(&bt_ui_weapon_icons[extra],150,334);
            else ui_bitmap(&bt_ui_item_small_icons[BT_LUMBERJACK_SHIRT],150,334);}
        unsigned rows=0;while(rows<5 && bt_ui_role_effects[g->setup_role][rows])++rows;
        int top=174+(5-(int)rows)*12;
        for(unsigned row=0;row<rows;++row){int icon=bt_ui_role_effect_icons[g->setup_role][row],y=top+(int)row*29;
            if(icon>=0)ui_bitmap(&bt_ui_stat_medium_icons[icon],213,y+9);
            else rect(209,y+6,5,5,255);
            role_effect(231,y,372,bt_ui_role_effects[g->setup_role][row]);}
        for(unsigned i=0;i<BT_ROLE_COUNT;++i){int x=24+(int)i*120;card(x,364,112,41,0,g->setup_role==i);
            ui_bitmap(&bt_ui_role_icons[i],x+23,384);
            if(i==BT_PACIFIST){text_limit(x+46,369,"和平",64);text_limit(x+46,387,"主义者",64);}
            else text_limit(x+46,379,bt_ui_role_names[i],64);}
    }else if(g->setup_field==1){
        unsigned slot=0;
        for(unsigned i=0;i<BT_WEAPON_KINDS;++i)if(bt_starting_masks[g->setup_role]&(1u<<i)){
            int x=28+(int)slot++*118;card(x,116,108,88,0,g->setup_weapon==i);
            ui_bitmap(&bt_ui_weapon_icons[i],x+54,151);centered(x+3,183,102,bt_ui_weapon_names[i]);}
        panel(30,226,580,164);ui_bitmap(&bt_ui_role_icons[g->setup_role],82,270);
        text(124,244,bt_ui_weapon_names[g->setup_weapon]);weapon_details(124,270,450,&bt_weapon_defs[g->setup_weapon][0]);
    }else if(g->setup_field==2){
        static const char *const descriptions[6]={UI_DANGER0,UI_DANGER1,UI_DANGER2,UI_DANGER3,UI_DANGER4,UI_DANGER5};
        /* Original ProgressData DEFAULT_TIER_COLOR_0..5, quantized to RGB332.
         * Danger 4/5 have their own orange/gold colors, not weapon tier 3. */
        static const uint8_t danger_colors[6]={255,87,171,228,236,217};
        for(unsigned i=0;i<6;++i){int x=34+(int)i*96;card_color(x,133,88,92,danger_colors[i],g->setup_difficulty==i);
            ui_bitmap(&bt_ui_danger_icons[i],x+44,166);number(x+42,198,(int32_t)i);}
        panel(58,251,524,135);text(82,268,UI_DIFFICULTY);number(154,268,(int32_t)g->setup_difficulty);
        wrapped(82,294,480,2,descriptions[g->setup_difficulty]);
        if(g->setup_difficulty)text(82,329,UI_DANGER_INHERIT);
        text(82,358,bt_ui_role_names[g->setup_role]);text(258,358,bt_ui_weapon_names[g->setup_weapon]);
    }else if(g->setup_field==3){
        button(72,140,228,144,g->setup_endless==0);centered(72,167,228,UI_CLASSIC);centered(72,220,228,"20 " UI_WAVE);
        button(340,140,228,144,g->setup_endless==1);centered(340,167,228,UI_ENDLESS);centered(340,220,228,"20+ " UI_WAVE);
        panel(72,305,496,90);centered(72,340,496,UI_SINGLE_MAP);
    }else{
        panel(82,123,476,235);ui_bitmap(&bt_ui_role_icons[g->setup_endless==2?BT_PACIFIST:g->setup_role],156,184);
        text(210,151,bt_ui_role_names[g->setup_endless==2?BT_PACIFIST:g->setup_role]);ui_bitmap(&bt_ui_weapon_icons[g->setup_endless==2?BT_HAND:g->setup_weapon],228,198);
        text(258,194,g->setup_endless==2?UI_SHOWCASE_BUILD:bt_ui_weapon_names[g->setup_weapon]);text(210,235,UI_DIFFICULTY);number(282,235,g->setup_endless==2?0:(int32_t)g->setup_difficulty);
        text(210,261,g->setup_endless==2?UI_SHOWCASE:g->setup_endless?UI_ENDLESS:UI_CLASSIC);button(198,303,244,32,1);centered(198,312,244,"A  " UI_START);
    }
    panel(18,412,604,56);centered(18,421,604,UI_SETUP_HINT);centered(18,442,604,UI_START_HINT "  " UI_GOD_HINT);
    if(g->test_invincible){rect(440,24,166,23,64);text(449,29,UI_TEST_GOD " TEST GOD");}
}
static void shop(const TatoGame *g)
{
    panel(12,12,616,36);heading(48,18,UI_SHOP);text(92,23,UI_WAVE);number(126,23,(int32_t)g->wave);
    bitmap(&bt_pickup_sprites[0],201,29,0);number(215,23,g->run.gold);
    button(346,17,124,26,g->selected==4);text(356,24,UI_REROLL);number(418,24,g->run.free_reroll?0:bt_reroll_price((int32_t)g->wave,(int32_t)g->run.rerolls,0));
    if(g->message) {
        static const char *const message[7]={"",UI_LOW_MATERIALS,UI_WEAPONS_FULL,UI_SOLD,UI_NO_PAIR,UI_ITEM_LIMIT,UI_ALL_LOCKED};
        if(g->message<7){rect(18,261,454,23,64);centered(18,266,454,message[g->message]);}
    }
    for(uint32_t i=0;i<4;++i) {
        const BtOffer *o=&g->run.offers[i];int x=12+(int)i*117;
        card(x,62,109,192,o->tier,g->selected==i);
        if(!o->active){centered(x,142,109,UI_SOLD);continue;}
        centered(x+3,72,103,o->weapon?bt_ui_weapon_names[o->id]:bt_ui_item_names[o->id]);
        ui_bitmap(o->weapon?&bt_ui_weapon_icons[o->id]:&bt_ui_item_icons[o->id],x+54,110);
        if(o->weapon){const BtWeaponDef *w=&bt_weapon_defs[o->id][o->tier];BtWeaponStats values,base;static const int32_t zero[BT_STAT_COUNT]={0};
            bt_prepare_weapon(&values,w,g->run.stat100,(w->set_mask&8)?bt_role_defs[g->run.role].unarmed_speed*100:0);
            bt_prepare_weapon(&base,w,zero,0);
            text(x+8,138,UI_DAMAGE);number_color(x+72,138,values.damage,difference_color(values.damage,w->damage));
            text(x+8,155,UI_COOLDOWN);cooldown_text(x+65,155,&values,difference_color(displayed_cooldown_ms(&base),displayed_cooldown_ms(&values)));
            text(x+8,172,UI_RANGE);number_color(x+72,172,values.range,difference_color(values.range,w->range));
        }else {int row=0;
            for(unsigned j=0;j<BT_STAT_COUNT && row<3;++j)if(bt_item_defs[o->id].effects[j]){
                int32_t value=bt_item_defs[o->id].effects[j];unsigned color=sign_color(value);
                ui_bitmap(&bt_ui_stat_icons[j],x+12,144+row*17);text_limit_color(x+21,138+row*17,bt_ui_stat_names[j],60,color);
                signed_number(x+79,138+row*17,value);++row;}
            if(!row)wrapped(x+8,139,93,3,o->id==BT_LUMBERJACK_SHIRT?UI_SHIRT_INFO:o->id==BT_STRUCTURE_ITEM_FIRST+BT_MINE?UI_MINE_INFO:UI_TURRET_INFO);
        }
        rect(x+5,198,99,26,g->selected==i?182:72);bitmap(&bt_pickup_sprites[0],x+25,211,0);number(x+45,205,bt_offer_price(&g->run,i));
        centered(x+3,234,103,o->locked?UI_LOCKED:UI_TIER);if(!o->locked)number(x+78,234,o->tier+1);
    }
    if(!g->message && g->selected<4 && g->run.offers[g->selected].active && g->run.offers[g->selected].weapon){
        panel(14,264,456,22);text(23,269,weapon_info((unsigned)g->run.offers[g->selected].id));
    }
    text(18,292,UI_WEAPONS);
    for(uint32_t i=0;i<g->run.weapon_count;++i) {
        int x=15+(int)i*77;const BtOwnedWeapon *w=&g->run.weapons[i];card(x,313,70,62,(unsigned)w->tier,g->selected==i+6);
        ui_bitmap(&bt_ui_weapon_icons[w->kind],x+30,334);number(x+55,320,w->tier+1);
        centered(x+3,357,64,bt_ui_weapon_names[w->kind]);
    }
    if(!g->run.weapon_count){card(15,313,455,62,0,g->selected==6);centered(15,337,455,UI_NO_WEAPONS);}
    text(18,384,UI_ITEMS);unsigned slot=0;
    for(unsigned i=0;i<BT_ITEM_COUNT;++i)if(g->run.items[i]){int x=72+(int)(slot%13)*30,y=391+(int)(slot/13)*24;
        ui_bitmap(&bt_ui_item_small_icons[i],x,y);if(g->run.items[i]>1)number(x+6,y-7,g->run.items[i]);++slot;}
    stats(g,490,62);button(490,434,136,32,g->selected==5);centered(490,444,136,UI_NEXT_WAVE "  Start");
    rect(8,446,472,34,0);text(16,447,UI_SHOP_HINT);text(16,464,UI_NEXT_HINT);
}
static void weapon_popup(const TatoGame *g)
{
    uint32_t slot=g->selected-6;if(slot>=g->run.weapon_count)return;
    const BtOwnedWeapon *owned=&g->run.weapons[slot];
    const BtWeaponDef *w=&bt_weapon_defs[owned->kind][owned->tier];
    BtWeaponStats values,base;static const int32_t zero[BT_STAT_COUNT]={0};
    bt_prepare_weapon(&values,w,g->run.stat100,(w->set_mask&8)?bt_role_defs[g->run.role].unarmed_speed*100:0);
    bt_prepare_weapon(&base,w,zero,0);
    focus_width=focus_height=0;
    card(32,65,352,361,(unsigned)owned->tier,0);
    centered(32,80,352,bt_ui_weapon_names[owned->kind]);
    ui_bitmap(&bt_ui_weapon_icons[owned->kind],83,133);
    text(52,169,UI_TIER);number(109,169,owned->tier+1);
    text(139,111,UI_DAMAGE);number_color(224,111,values.damage,difference_color(values.damage,w->damage));
    text(139,131,UI_COOLDOWN);cooldown_text(224,131,&values,difference_color(displayed_cooldown_ms(&base),displayed_cooldown_ms(&values)));text(269,131,UI_SECONDS);
    text(139,151,UI_RANGE);number_color(224,151,values.range,difference_color(values.range,w->range));
    wrapped(52,193,312,3,weapon_info((unsigned)owned->kind));
    static const char *const names[BT_SET_COUNT]={"枪械","精准","原始","徒手","工具","辅助"};
    unsigned panel_index=0;
    for(unsigned i=0;i<BT_SET_COUNT && panel_index<2;++i)if(w->set_mask&(1u<<i)){
        unsigned count=0;for(unsigned j=0;j<g->run.weapon_count;++j)
            count+=(bt_weapon_defs[g->run.weapons[j].kind][g->run.weapons[j].tier].set_mask&(1u<<i))!=0;
        int y=65+(int)panel_index++*181;panel(398,y,224,174);
        text(410,y+12,names[i]);text(498,y+12,"(");number(506,y+12,(int32_t)count);text(516,y+12,"/6)");
        for(unsigned n=2;n<=6;++n){int row=y+41+(int)(n-2)*24;unsigned color=count==n?1u:3u;
            text_color(410,row,"(",color);number_color(418,row,(int32_t)n,color);text_color(428,row,") +",color);
            number_color(452,row,bt_set_defs[i].bonus[n-2],color);text_limit_color(476,row,bt_ui_stat_names[bt_set_defs[i].stat],136,color);
        }
    }
    if(bt_run_can_combine(&g->run,slot)){
        button(52,265,312,32,g->inventory_slot==BT_POPUP_COMBINE);centered(52,275,312,UI_COMBINE);
    }
    button(52,307,312,32,g->inventory_slot==BT_POPUP_RECYCLE);
    int32_t price=bt_run_recycle_value(&g->run,slot),digits=1;
    for(int32_t n=price;n>=10;n/=10)++digits;
    text(138,317,UI_RECYCLE);text(188,317,"(+");number(202,317,price);text(202+digits*6,317,")");
    button(52,349,312,32,g->inventory_slot==BT_POPUP_CANCEL);centered(52,359,312,UI_CANCEL);
    centered(32,400,352,UI_POPUP_HINT);
}
static void item_detail(const TatoGame *g,int32_t id,int crate)
{
    focus_width=focus_height=0;
    card(105,78,430,324,(unsigned)bt_item_defs[id].tier,0);centered(105,97,430,bt_ui_item_names[id]);ui_bitmap(&bt_ui_item_icons[id],320,146);
    int row=0;
    if(id==BT_LUMBERJACK_SHIRT)centered(105,193,430,UI_SHIRT_INFO);
    if(id>=BT_STRUCTURE_ITEM_FIRST && id<BT_STRUCTURE_ITEM_FIRST+BT_STRUCTURE_KINDS){
        uint32_t kind=(uint32_t)(id-BT_STRUCTURE_ITEM_FIRST);const BtStructureDef *d=&bt_structure_defs[kind];
        text(132,192,kind==BT_MINE?UI_MINE_INFO:UI_TURRET_INFO);
        text(132,218,d->healing?UI_HEAL:d->burn_duration?UI_BURN_DAMAGE:UI_DAMAGE);
        number(258,218,d->burn_duration?bt_weapon_damage(d->burn_damage,d->burn_scale100,g->run.stat100[BT_ENGINEERING],0):bt_structure_damage(g,kind));
        text(132,244,UI_ENGINEERING_INFO);
        if(d->burn_duration)text(132,270,UI_BURN_INFO);
        else if(kind==BT_MINE)text(132,270,UI_MINE_TRIGGER);
        else if(d->healing)text(132,270,UI_HEALING_INFO);
        else if(d->explosion_scale100)text(132,270,UI_EXPLOSION_INFO);
        else if(d->shot.piercing)text(132,270,UI_PIERCING_INFO);
    }
    for(uint32_t i=0;i<BT_STAT_COUNT;++i)if(bt_item_defs[id].effects[i]) {
        int x=126+(row%2)*200,y=184+(row/2)*25;
        int32_t value=bt_item_defs[id].effects[i];unsigned color=sign_color(value);
        ui_bitmap(&bt_ui_stat_icons[i],x+3,y+6);text_limit_color(x+15,y,bt_ui_stat_names[i],132,color);signed_number(x+150,y,value);++row;
    }
    if(crate) {
        text(158,355,UI_CRATE_HINT);bitmap(&bt_pickup_sprites[0],365,362,0);
        number(382,355,bt_recycle_price((int32_t)g->wave,bt_item_defs[id].value,0,g->run.stat100[BT_RECYCLING_GAINS]/100,0));
    } else centered(105,371,430,UI_CLOSE_HINT);
}
void tato_render(const TatoGame *g)
{
    focus_width=focus_height=0;camera_active=0;
#ifndef TATO_HOST
    /* Preserve the accepted640 composition; primitives map to native800. */
    game_viewport(640,480);
    /* BITMAP writes pixels but only a full FILL validates a newly reset bank. */
    game_clear(0);if(initial_clears)--initial_clears;
    native800_clip(0,0,640,480);
#endif
    if(g->mode==TATO_TITLE && g->setup_field==BT_SETUP_HOME){
        unsigned phase=g->frame%360u,half=phase<=180u?phase:360u-phase;
        unsigned ease=half*half*(540u-2u*half);
        unsigned pose=(24u*ease+2916000u)/5832000u;
#ifdef TATO_HOST
        bitmap_key(&bt_home_background[pose],0,0,0,-1);
#else
        native800_layout640(0);
        for(unsigned row=0;row<19;row++)for(unsigned col=0;col<25;col++){
            const HomeTile800 *part=&home_detail_tiles[home_detail_poses[pose][row*25+col]];
            if(part->solid){if(part->color)native800_fill((int)col*32,(int)row*32,32,32,part->color);}
            else native800_bitmap_physical(part->pixels,32,32,32,(int)col*32,(int)row*32,0,0,0,4);
        }
        native800_layout640(1);
#endif
    }
    else if(g->mode==TATO_TITLE || g->mode==TATO_SHOP)bitmap_key(&bt_ui_backdrop,0,0,0,-1);
    else {
        camera_select(g);
        background();
        if(g->mode==TATO_COLLECT){
            for(uint32_t i=0;i<TATO_DROPS;++i)if(g->drops[i].active)
                bitmap_key(&bt_green_material,screen_x(g->drops[i].x),screen_y(g->drops[i].y),0,BT_VISIBILITY_KEY);
            bitmap(&WORLD_ROLE_SPRITES[g->run.role],screen_x(g->x),screen_y(g->y),0);
            centered(0,425,640,UI_BAG);
        }else world(g);
        camera_active=0;hud(g);
    }
    if(g->mode==TATO_TITLE)title(g);
    else if(g->mode==TATO_SHOP) {
        shop(g);
        if(g->inventory_slot==BT_POPUP_ITEM && g->selected<4 && g->run.offers[g->selected].active && !g->run.offers[g->selected].weapon)
            item_detail(g,g->run.offers[g->selected].id,0);
        else if(g->inventory_slot>=BT_POPUP_COMBINE && g->selected>=6)weapon_popup(g);
    }
    else if(g->mode==TATO_UPGRADE) {
        panel(12,79,468,299);heading(246,95,UI_UPGRADE);
        for(uint32_t i=0;i<4;++i) {
            int x=20+(int)i*114;card(x,147,106,151,g->upgrade_tiers[i],g->selected==i);
            const BtUpgradeDef *d=&bt_upgrade_defs[g->upgrade_choices[i]];
            ui_bitmap(&bt_ui_stat_large_icons[d->stat],x+53,181);
            centered(x+3,217,100,bt_ui_stat_names[d->stat]);text_color(x+39,252,"+",1);number_color(x+51,252,d->value[g->upgrade_tiers[i]],1);
        }
        button(126,318,240,31,g->selected==4);text(155,328,UI_REROLL);number(307,328,bt_reroll_price((int32_t)g->wave,(int32_t)g->upgrade_rerolls,0));
        centered(12,359,468,UI_UPGRADE_HINT);stats(g,490,62);
    } else if(g->mode==TATO_CRATE) {
        if(g->crate_item>=0)item_detail(g,g->crate_item,1);
    } else if(g->mode==TATO_PAUSE) {
        panel(158,105,324,340);heading(320,123,UI_PAUSE);
        button(184,164,272,34,1);centered(184,175,272,"A / B / Start  " UI_RESUME);
        text(192,222,UI_VOLUME);number(408,222,(int32_t)arena_audio_volume());
        for(unsigned i=0;i<8;++i)rect(192+(int)i*30,247,25,9,i<arena_audio_volume()?44:73);
        centered(158,279,324,arena_audio_muted()?UI_MUTED:UI_AUDIO_ON);
        centered(158,302,324,"LB/RB  摇杆死区");
        unsigned preset=20,preset_valid=1;
#ifndef TATO_HOST
        if(*(volatile uint32_t *)(uintptr_t)0x10000124u==0x49500100u){
            unsigned snapshot=*(volatile uint32_t *)(uintptr_t)0x10000134u;
            preset=(snapshot>>22)&63u;preset_valid=snapshot>>31;
        }
#endif
        if(preset_valid){number(410,302,(int32_t)preset);text(434,302,"%");}
        else text(410,302,"--");
        centered(158,326,324,"X 静音  左右调音量");centered(158,350,324,"Select  " UI_HOME);
        PlatformInput axes={0x10000u,0,0,0x85000000u};
#ifndef TATO_HOST
        input_poll(&axes);
#endif
        if((axes.buttons&0x10000u) && (axes.aux&0x80000000u)){
            text(192,374,"左摇杆");text(306,374,"X");text(384,374,"Y");
            number(328,374,(int16_t)axes.left*100/32767);number(406,374,(int16_t)(axes.left>>16)*100/32767);
            text(192,395,"右摇杆");text(306,395,"X");text(384,395,"Y");
            number(328,395,(int16_t)axes.right*100/32767);number(406,395,(int16_t)(axes.right>>16)*100/32767);
            text(192,416,"LT / RT");number(328,416,(int32_t)(axes.aux&255u)*100/255);number(406,416,(int32_t)((axes.aux>>8)&255u)*100/255);
        }else centered(158,395,324,"手柄未连接");
    } else if(g->mode==TATO_DEAD || g->mode==TATO_WIN) {
        panel(114,108,412,266);heading(320,122,g->mode==TATO_WIN?UI_WIN:UI_DEAD);
        ui_bitmap(&bt_ui_role_icons[g->run.role],320,186);centered(114,222,412,bt_ui_role_names[g->run.role]);
        text(158,258,UI_WAVE);number(211,258,(int32_t)g->wave);text(348,258,UI_KILLS);number(401,258,(int32_t)g->kills);
        centered(114,307,412,UI_END_HINT);
    } else if(g->mode==TATO_FAULT) {
        panel(104,154,432,180);centered(104,174,432,UI_FAULT);text(158,220,UI_FAULT_CODE);number(302,220,(int32_t)g->fault);
        centered(104,283,432,"Select  " UI_HOME);
    }
}
