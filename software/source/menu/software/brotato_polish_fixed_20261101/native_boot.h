#ifndef LONGRUN_NATIVE_BOOT_H
#define LONGRUN_NATIVE_BOOT_H
#include <stdint.h>
/* Owner-only boot preferences in the existing atomic input snapshot. Field
 * value63 is accepted by RTL and outside physical deadzone presets10/20/30/50.
 * Bits28..31 cannot be written through the PS staging port. */
#ifndef NATIVE_BOOT_READ
#if defined(TATO_HOST) || defined(MENU_HOST) || defined(TH_HOST)
#define NATIVE_BOOT_READ(o) 0u
#else
#define NATIVE_BOOT_READ(o) (*(volatile uint32_t *)(uintptr_t)(0x10000100u+(o)))
#endif
#endif
static inline uint32_t native_boot_packet(void)
{
    if(NATIVE_BOOT_READ(36)!=0x49500100u)return 0;
    for(unsigned i=0;i<3;i++){
        uint32_t seq=NATIVE_BOOT_READ(40),value=NATIVE_BOOT_READ(44);
        uint32_t marker=NATIVE_BOOT_READ(48),aux=NATIVE_BOOT_READ(52);
        if(seq!=NATIVE_BOOT_READ(40))continue;
        if(marker==0x41554430u && (aux&0x8fc00000u)==0x8fc00000u &&
           !(value&~0xf0fu) && (value&15u)<=8u && ((value>>9)&7u)<6u)return 0x80000000u|value;
        return 0;
    }
    return 0;
}
static inline uint32_t native_boot_audio(void)
{
    uint32_t packet=native_boot_packet();return packet?packet&0x10fu:0x103u;
}
static inline unsigned native_boot_menu_selection(void)
{
    return (native_boot_packet()>>9)&7u;
}
#endif
