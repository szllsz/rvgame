/* Native RV32I card launcher; same application mailbox as the accepted menu. */
#include <stdint.h>
#include <stddef.h>
#include "platform_input.h"
#include "pointer_native.h"
static unsigned menu_pointer_previous;
static unsigned menu_pointer_position=~0u;
#include "input_tone.h"
#include "game_gfx.h"
#include "work_ui.h"
#include "native800_menu_characters.h"
#include "native_boot.h"
#define DRAW(o) (*(volatile uint32_t *)(uintptr_t)(0x10000200u+(o)))
#define STATE(o) (*(volatile uint32_t *)(uintptr_t)(0x10000100u+(o)))
static unsigned animation_frame,reveal=8,loading_classic,loading_app,loading_stage;
static unsigned menu_muted=1;
#ifdef MENU_BROTATO_SESSION
static unsigned session_final,session_next,session_unavailable;
#endif
#ifndef MENU_HOST
#include "brotato_fidelity_20261027/bt_mailbox.h"
static uint32_t gpu_before,gpu_after;
static uint32_t menu_sequence,draw_cycles;
static BtMailbox mailbox;
static unsigned last_selection;
static InputRepeat navigation;
static InputConnection connection;
static InputTone connection_tone;
static unsigned menu_volume=3;
void *memset(void *ptr,int value,size_t size)
{uint8_t *p=ptr;while(size--) *p++=(uint8_t)value;return ptr;}
#endif
#ifdef MENU_HOST
void menu_host_rect(unsigned x,unsigned y,unsigned w,unsigned h,unsigned color);
#endif
static void rect(unsigned x,unsigned y,unsigned w,unsigned h,unsigned color)
{
#ifdef MENU_HOST
    menu_host_rect(x,y,w,h,color);
#else
    native800_fill((int)x,(int)y,(int)w,(int)h,color);
#endif
}
static void label(unsigned x,unsigned y,const char *s,unsigned palette)
{
    ui_text(x,y,s,palette);
}
static void outline(unsigned x,unsigned y,unsigned w,unsigned h,unsigned color)
{rect(x,y,w,3,color);rect(x,y+h-3,w,3,color);rect(x,y,3,h,color);rect(x+w-3,y,3,h,color);}
static void icon(unsigned i,unsigned x,unsigned y,unsigned selected)
{
    static const unsigned colors[]={0x3f,0x9d,0x5f,0xb7,0xf4,0x7f};
    unsigned accent=colors[i];
    rect(x,y,92,82,0x09);rect(x+4,y+4,84,2,accent);
    if(i==0) {
        rect(x+26,y+18,44,46,0x01);rect(x+22,y+14,44,46,accent);
        rect(x+27,y+19,34,36,0x09);outline(x+30,y+22,28,30,0x32);
        for(unsigned k=0;k<4;++k){rect(x+14,y+19+k*10,8,4,accent);rect(x+66,y+19+k*10,8,4,accent);}
        label(x+32,y+29,"CPU",0);rect(x+30,y+64,28,3,0x32);
    } else if(i==1) {
        for(unsigned k=0;k<9;++k){unsigned tx=x+15+(k%3)*22,ty=y+13+(k/3)*21;
            rect(tx+3,ty+3,17,16,0x01);rect(tx,ty,17,16,k&1?0x7d:0xbd);rect(tx,ty,17,3,0xdf);}
    } else if(i==2) {
        rect(x+19,y+14,62,48,0x01);outline(x+15,y+10,62,48,0x32);
        rect(x+10,y+22,64,45,accent);rect(x+14,y+26,56,37,0x0b);rect(x+20,y+31,9,9,0xfc);
        for(unsigned k=0;k<17;++k)rect(x+17+k*3,y+51-(k<9?k:16-k)*2,3,10+(k<9?k:16-k)*2,0x5c);
        for(unsigned k=0;k<6;++k)rect(x+19+k*9,y+70,5,4,0xbd);
    } else if(i==3) {
        rect(x+14,y+15,40,40,0x81);rect(x+18,y+19,32,32,0xe5);
        rect(x+38,y+34,40,36,0x16);rect(x+42,y+38,32,28,0x3f);
        rect(x+38,y+34,16,21,0xb7);rect(x+42,y+38,8,13,0xdf);
        rect(x+18,y+19,28,3,0xef);rect(x+46,y+63,24,3,0x5f);
    } else if(i==4) {
        const MenuCharacter800 *p=&menu_potato_poses[selected?animation_frame%90:0];
        native800_bitmap_physical(p->pixels,p->width,p->width,p->height,
            native800_layout_coordinate((int)x)+p->dx,native800_layout_coordinate((int)y)+p->dy,0,0,1u|(227u<<8),4);
    } else {
        unsigned t=selected?animation_frame%50:0;
        const MenuCharacter800 *p=&menu_remilia_poses[t<8?0:t<16?1:t<24?2:t<32?3:t<42?4:5];
        native800_bitmap_physical(p->pixels,p->width,p->width,p->height,
            native800_layout_coordinate((int)x)+p->dx,native800_layout_coordinate((int)y)+p->dy,0,0,1u|(227u<<8),4);
    }
}
static void draw(unsigned selection,unsigned focus_x,unsigned focus_y)
{
#ifndef MENU_HOST
    native800_begin();
    for(unsigned band=0;band<2;band++){
        native800_band(band);native800_clear(0x05);
#endif
    static const char *names[]={"CPU 测评","TILE 图块","DDR 位图","Alpha 混合","鸡蛋兄弟","星际弹幕"};
    static const unsigned widths[]={64,72,64,80,64,64};
    static const char *details[]={"CPU：标准性能测试与 CRC 正确性验证","图块：同场景软件绘制与硬件加速对照","位图：DDR 图像复制、翻转与透明色","混合：五档 Alpha 与 Color Key 联合演示","鸡蛋兄弟：普通游戏与性能展示","星际弹幕：飞船战斗与同场景软硬对照"};
    /* Plain deep blue: all visual feedback stays on the icons/cards. */
    rect(0,0,640,480,0x05);
    label(28,20,"基于自研 RISC-V 的二维图形加速与实时交互 SoC",0);
#ifdef MENU_BROTATO_SESSION
    label(28,50,"鸡蛋兄弟",1);
#else
    label(28,50,"CPU 测评  /  图形实验  /  互动游戏",1);
#endif
    NativePointer hover=pointer_read();
    outline(focus_x,focus_y,140,140,(animation_frame/24)&1?0x7f:0x3f);
    for(unsigned i=0;i<6;++i) {
#ifdef MENU_BROTATO_SESSION
        if(i!=4)continue;
#endif
        unsigned x=54+(i%3)*200,y=100+(i/3)*164;
#ifndef MENU_HOST
        if(pointer_hit640(hover,(int)x,(int)y,132,132))native800_pointer_style(1);
#else
        (void)hover;
#endif
        rect(x+4,y+5,132,132,0x01);
        if(i==selection && reveal==8)y-=2;
        rect(x,y,132,132,i==selection?0x12:0x0a);
        icon(i,x+20,y+10,i==selection);
        if(i==selection){unsigned shine=(animation_frame/10)%6;
            rect(x+25+shine*12,y+17,8,2,0xff);rect(x+18,y+14,3,16,0x3f);}
        if(i==selection)rect(x+46,y+98,40,2,0x3f);
#ifdef MENU_BROTATO_SESSION
        label(x+(132-widths[i])/2,y+105,names[i],i==selection?0:1);
#else
        label(x+(132-widths[i])/2,y+105,names[i],i==selection?0:1);
#endif
    }
    rect(28,398,584,18,0x09);label(36,399,details[selection],1);
    rect(24,420,592,1,0x12);
    label(28,438,"手柄 A 进入  /  触屏双击  /  鼠标单击",0);
    label(420,438,menu_muted?"Y 取消静音":"Y 静音",1);
#ifdef MENU_BROTATO_SESSION
    label(28,461,session_unavailable?"其他应用请退出预览后使用":"START退出预览并恢复正式菜单",1);
#else
    label(28,461,"SELECT 可从任意程序返回此主菜单",1);
#endif
#ifndef MENU_HOST
        native800_flush(band);
    }
#endif
}
static unsigned move(unsigned selection,unsigned pressed)
{
#ifdef MENU_BROTATO_SESSION
    (void)selection;(void)pressed;return 4;
#endif
    if(pressed&64u) return selection?selection-1:5;
    if(pressed&128u) return selection==5?0:selection+1;
    if(pressed&(16u|32u)) return (selection+3)%6;
    return selection;
}
#ifndef MENU_BROTATO_SESSION
static void application_loading(unsigned selection,unsigned stage)
{
    static const char *names[]={"CPU 测评","TILE 图块","DDR 位图","Alpha 混合","鸡蛋兄弟","星际弹幕"};
    loading_stage=stage;
#ifndef MENU_HOST
    native800_begin();native800_pointer_style(3);
    for(unsigned band=0;band<2;band++){
        native800_band(band);native800_clear(0x05);
#endif
    rect(0,0,640,480,0x05);rect(100,138,440,198,0x0a);
    label(132,164,"正在加载",0);label(132,202,names[selection],0);
    rect(132,246,376,14,0x01);rect(134,248,372,10,0x12);
    /* Two real stages: request accepted, then final retirement requested.
     * The next ready application completes the transition; no timed fake %. */
    rect(134,248,stage==1?112:248,10,0x3f);
    label(132,282,stage==1?"准备切换":"正在切换，请稍候",1);
    if(selection>=4)label(132,310,"游戏中按SELECT返回应用中心",1);
#ifndef MENU_HOST
        native800_flush(band);
    }
#endif
}
#endif
#ifdef MENU_HOST
/* The owner uses these same-font banners only after the old RV32 renderer
 * has frozen. Two actual switch stages; no timer-derived percentage. */
static void transition_banner(unsigned target,unsigned stage)
{
    static const char *names[]={"正在进入土豆兄弟","正在进入红魔乡","正在返回应用中心"};
    rect(0,416,640,64,0x09);label(24,422,names[target],0);
    (void)stage;
    rect(24,454,592,12,0x01);rect(26,456,588,8,0x12);
}
#endif
#ifndef MENU_HOST
static void menu_telemetry(unsigned selection,int final)
{
    if(bt_mailbox_advance(&mailbox,STATE(28),final)) {
        if(final&&(STATE(28)&4u)) {
            bt_mailbox_retirement(&mailbox,1u,menu_volume|(menu_muted<<8),0u,DRAW(16)&8u);
            STATE(0)=bt_mailbox_symbol(&mailbox);return;
        }
        uint32_t motion[8]={0x55493630u,menu_sequence,draw_cycles,animation_frame,selection,reveal|(loading_classic<<8),STATE(24),~menu_sequence};
        uint32_t data[8]={0x4d4e5531u,selection,gpu_before,gpu_after,DRAW(120),DRAW(124),STATE(24),~selection};
        for(unsigned i=0;i<8;++i){mailbox.words[i]=motion[i];mailbox.words[8+i]=data[i];}
        mailbox.words[16]=DRAW(16);
        mailbox.words[17]=*(volatile uint32_t *)(uintptr_t)0x10000384u;
        mailbox.words[20]=0x41550000u|menu_volume|(menu_muted<<8);
#ifdef MENU_BROTATO_SESSION
        mailbox.words[18]=session_next;
#else
        mailbox.words[18]=loading_stage;mailbox.words[19]=loading_app;
#endif
        bt_mailbox_seal(&mailbox);
    }
    STATE(0)=bt_mailbox_symbol(&mailbox);
}
static void present(unsigned page,unsigned selection)
{
    game_wait();
    /* STATE writes are ignored while the previous scan snapshot is pending. */
    while(STATE(20)&1u) {}
    ++menu_sequence;last_selection=selection;
    if(page==0x3a0)menu_telemetry(selection,0);
    else STATE(0)=selection; /* Preserve the actual launch selection interface. */
    STATE(4)=page;STATE(20)=1;
    if(page==0x3a0) {game_wait();}
    else {
        while(STATE(20)&1u) {}
        /* Keep the launch choice stable until PS requests the immutable final packet. */
        for(;;)if(STATE(28)&2u){
            unsigned ack=STATE(28);
            if(mailbox.final && (mailbox.done || (ack&1u)!=mailbox.phase))continue;
#ifndef MENU_BROTATO_SESSION
            if(!mailbox.final && loading_classic){
                application_loading(loading_app,2);game_wait();
            }
#endif
            menu_telemetry(selection,1);STATE(4)=0x35f;STATE(20)=1;
            while(STATE(20)&1u){}
        }
    }
}
int main(void)
{
    if(DRAW(128)!=(600u<<16|800u)){STATE(4)=0x3ff;STATE(20)=1;for(;;){}}
    uint32_t boot_audio=native_boot_audio();menu_volume=boot_audio&15u;menu_muted=(boot_audio>>8)&1u;
    *(volatile uint32_t *)(uintptr_t)0x10000380u=menu_volume;
    *(volatile uint32_t *)(uintptr_t)0x10000384u=menu_muted;
    input_tone_init();
    game_wait();gpu_before=DRAW(16);DRAW(12)=4;game_wait();gpu_after=DRAW(16);
    DRAW(124)=4;game_viewport(640,480);
    uint8_t indices[32],rgb[192]={0};for(unsigned i=0;i<32;++i)indices[i]=i;
    rgb[3]=rgb[4]=rgb[5]=255;rgb[15]=144;rgb[16]=188;rgb[17]=216;
    ui_init();native800_begin();native800_band(0);game_palette(indices,rgb);
    native800_clear(0x05);native800_flush(0);
    native800_band(1);native800_clear(0x05);native800_flush(1);
    unsigned selection=native_boot_menu_selection(),previous=STATE(32)&4095u;
    int fx=50+(int)(selection%3)*200,fy=96+(int)(selection/3)*164,start_x=fx,start_y=fy;unsigned step=8,pending=0;
#ifdef MENU_BROTATO_SESSION
    selection=4;fx=start_x=250;fy=start_y=260;
#endif
    /* Static cards need one initial draw. Avoid ignoring input during eight
     * identical reveal frames when entering the menu from a live session. */
    reveal=8;
    uint32_t begin=DRAW(80);draw(selection,fx,fy);draw_cycles=DRAW(80)-begin;
    present(0x3a0,selection);animation_frame=STATE(24);
    for(;;) {
        if((STATE(28)&2u) || mailbox.final
#ifdef MENU_BROTATO_SESSION
           || session_final
#endif
        ) {
            if(STATE(28)&2u){
                unsigned ack=STATE(28);
                /* Wait for the host ACK without re-committing the same symbol. */
                if(mailbox.final && (mailbox.done || (ack&1u)!=mailbox.phase))continue;
                while(STATE(20)&1u) {}
                menu_telemetry(last_selection,1);STATE(4)=0x35f;
            }
            else STATE(4)=0x35e;
            STATE(20)=1;
            while(STATE(20)&1u){}continue;
        }
        unsigned boot_active=native_boot_packet()!=0;
        unsigned word=boot_active?0:STATE(32),buttons=word&4095u,pressed=buttons&~previous;previous=buttons;
        pressed=input_repeat(&navigation,buttons,pressed,DRAW(80),1u);
        if(pressed&256u)menu_muted^=1;
        if((pressed&1024u) && menu_volume)--menu_volume;
        if((pressed&2048u) && menu_volume<8)++menu_volume;
        if(pressed&(256u|1024u|2048u)){
            *(volatile uint32_t *)(uintptr_t)0x10000380u=menu_volume;
            *(volatile uint32_t *)(uintptr_t)0x10000384u=menu_muted;
        }
        uint32_t input_now=DRAW(80);
        if(!boot_active)input_tone_tick(&connection_tone,input_connection(&connection,word&0x10000u,input_now),input_now);
#ifdef MENU_BROTATO_SESSION
        if(pressed&8u){session_final=1;session_next=0;continue;}
#endif
        unsigned next=move(selection,pressed);
        NativePointer pointer=pointer_read();
        unsigned clicked=pointer_press(pointer,&menu_pointer_previous,1);
        unsigned point=pointer.valid?pointer.x|(pointer.y<<16):~0u,moved=point!=menu_pointer_position;
        menu_pointer_position=point;
        for(unsigned i=0;i<6;i++)if((moved||clicked) && !(pressed&0xf0u) && pointer_hit640(pointer,54+(int)(i%3)*200,100+(int)(i/3)*164,132,132)){
            next=i;
        }
        if(next!=selection){selection=next;start_x=fx;start_y=fy;step=0;}
#ifndef MENU_BROTATO_SESSION
        if(pressed&2u)pending=1;
#else
        if(pressed&2u)pending=1;
#endif
        if(pending && step==8){
#ifdef MENU_BROTATO_SESSION
            if(selection==4 && pending==1){session_next=1;session_final=1;continue;}
            session_unavailable=1;pending=0;
#else
            loading_classic=1;loading_app=selection;application_loading(selection,1);
            present(0x3a0,selection);
            present(pending==2?0x3a2:0x3a1,selection);for(;;){}
#endif
        }
        uint32_t now=STATE(24);
        /* One update per scan tick: keep focus movement and idle feedback smooth. */
        if(now==animation_frame)continue;
        animation_frame=now;
        if(step<8) {
            ++step;int ease=(int)(step*step*(24-2*step));
            int tx=50+(int)(selection%3)*200,ty=96+(int)(selection/3)*164;
            fx=start_x+(tx-start_x)*ease/512;fy=start_y+(ty-start_y)*ease/512;
        }
        uint32_t begin=DRAW(80);draw(selection,(unsigned)fx,(unsigned)fy);game_wait();draw_cycles=DRAW(80)-begin;
        present(0x3a0,selection);
    }
}
#endif
