/* Existing STATE.x (10 data bits + alternating bit), acknowledged through
 * existing host_buttons.bit0. No framebuffer bytes or hardware changes.
 * 80 original telemetry words + word-wise FNV checksum, little-bit first.
 * V2 tags: 00 + six-bit zero run minus one; 01/u8; 10/u16; 11/u32.
 * Packet marker, payload symbol count, then packed ten-bit symbols.
 * Host bit1 requests a final, frozen snapshot before hold/drain/free. */
#ifndef BT_MAILBOX_H
#define BT_MAILBOX_H
#include <stdint.h>
typedef struct {
    uint32_t words[81];
    uint32_t packed[87],bits,count;
    uint32_t cursor,phase,started,final,done;
} BtMailbox;
static inline int bt_mailbox_advance(BtMailbox *m,uint32_t ack,int final)
{
    if(!m->started || (final && !m->final)) {
        m->started=1;m->final=(uint32_t)final;m->phase=(ack^1u)&1u;m->cursor=0;return 1;
    }
    if((ack&1u)!=m->phase || m->done)return 0;
    if(m->cursor==m->count+1) {
        if(m->final){m->done=1;return 0;}
        m->cursor=0;m->phase^=1;return 1;
    }
    ++m->cursor;m->phase^=1;return 0;
}
static inline void bt_mailbox_put(BtMailbox *m,uint32_t value,unsigned width)
{
    unsigned word=m->bits>>5,shift=m->bits&31;
    m->packed[word]|=value<<shift;
    if(shift && width>32-shift)m->packed[word+1]|=value>>(32-shift);
    m->bits+=width;
}
static inline void bt_mailbox_seal(BtMailbox *m)
{
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<80;++i)hash=(hash^m->words[i])*16777619u;
    m->words[80]=hash;
    for(unsigned i=0;i<87;++i)m->packed[i]=0;
    m->bits=0;
    for(unsigned i=0;i<81;) {
        uint32_t value=m->words[i];
        if(!value) {
            unsigned run=1;
            while(run<64 && i+run<81 && !m->words[i+run])++run;
            bt_mailbox_put(m,(run-1)<<2,8);i+=run;
        } else {
            unsigned tag=value<=255?1:value<=65535?2:3;
            bt_mailbox_put(m,tag,2);
            bt_mailbox_put(m,value,tag==1?8:tag==2?16:32);++i;
        }
    }
    m->count=(m->bits+9)/10;
}
/* With final bit1 set, host bit2 selects the interactive retirement packet.
 * Outside final retirement bit2 retains its existing diagnostic meaning.
 * Keep the same alternating-bit/FNV transport, but omit diagnostic counters. */
static inline void bt_mailbox_retirement(BtMailbox *m,uint32_t application,
                                         uint32_t audio,uint32_t route,uint32_t fault)
{
    for(unsigned i=0;i<80;++i)m->words[i]=0;
    m->words[0]=0x52545231u; /* RTR1: never a complete telemetry snapshot. */
    m->words[1]=application|(audio<<3)|(route<<12);
    m->words[2]=fault;
    bt_mailbox_seal(m);
}
static inline uint32_t bt_mailbox_symbol(const BtMailbox *m)
{
    uint32_t value;
    if(!m->cursor)value=m->final?0x3a8u:0x3a7u;
    else if(m->cursor==1)value=m->count;
    else {
        unsigned bit=(m->cursor-2)*10,word=bit>>5,shift=bit&31;
        value=m->packed[word]>>shift;
        if(shift>22 && word<86)value|=m->packed[word+1]<<(32-shift);
        value&=1023u;
    }
    return value|(m->phase<<10);
}
#endif
