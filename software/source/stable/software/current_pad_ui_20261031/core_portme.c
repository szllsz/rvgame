/* rvgame port only; official benchmark algorithms remain unchanged. */
#include "coremark.h"
#define REG(a) (*(volatile ee_u32 *)(uintptr_t)(a))
#ifdef CORE_HOST
#undef REG
ee_u32 *core_host_reg(ee_u32 address);
#define REG(a) (*core_host_reg(a))
#endif
#if VALIDATION_RUN
volatile ee_s32 seed1_volatile=0x3415, seed2_volatile=0x3415;
#else
volatile ee_s32 seed1_volatile=0, seed2_volatile=0;
#endif
volatile ee_s32 seed3_volatile=0x66, seed4_volatile=ITERATIONS, seed5_volatile=0;
ee_u32 default_num_contexts=1;
static ee_u32 started, stopped, log_size;
volatile ee_u32 platform_clock_hz=PLATFORM_CLOCK_HZ;
static char output[4096];
static unsigned log_overflow,phase_begin;
#if COREMARK_SUITE
static unsigned suite_phase,suite_details;
static ee_u32 suite_ticks[2];
static int suite_valid[2];
static ee_u16 suite_crc[2][3];
void coremark_suite_page(const ee_u32 *ticks,const int *valid,const ee_u16 crc[2][3],unsigned phase,unsigned details);
int coremark_run(void);
#endif
void coremark_page(ee_u32 ticks,int valid);
void coremark_present(void);
void coremark_log(const char *text,unsigned size);
void start_time(void) { started=REG(0x10000250u); }
void stop_time(void) { stopped=REG(0x10000250u); }
CORE_TICKS get_time(void) { return stopped-started; }
secs_ret time_in_secs(CORE_TICKS ticks) { return ticks/platform_clock_hz; }
void uart_send_char(char c) {
    if(log_size<sizeof(output)-1) output[log_size++]=c;
    else log_overflow=1;
}
void *memset(void *dest,int c,size_t n) {
    unsigned char *p=dest; while(n--) *p++=(unsigned char)c; return dest;
}
void *memcpy(void *dest,const void *src,size_t n) {
    unsigned char *p=dest;const unsigned char *q=src;while(n--) *p++=*q++;return dest;
}
static void page(ee_u32 value) {
    while(REG(0x10000114u)&1u) {}
    REG(0x10000100u)=0;REG(0x10000104u)=value;REG(0x10000114u)=1;
    while(REG(0x10000114u)&1u) {}
}
static void return_requested(void){
    /* Outside start_time/stop_time: finish a timed kernel before acknowledging
     * an exit. A stable exit page admits HOLD without a native half-frame. */
    if(REG(0x10000120u)&4u){page(0x362);for(;;){}}
}
static void result_page(int valid,unsigned diagnostic) {
    coremark_log(diagnostic?output:0,diagnostic?log_size:0);
#if COREMARK_SUITE
    (void)valid;
    coremark_suite_page(suite_ticks,suite_valid,suite_crc,suite_phase,suite_details);
#else
    coremark_page(get_time(),valid);
#endif
    /* The native transcript remains available to explicit diagnostic requests.
     * Ordinary result pages keep the bottom eight rows as UI background. */
    coremark_present();
}
void portable_init(core_portable *p,int *argc,char *argv[]) {
    (void)argc;(void)argv; p->portable_id=1;
    phase_begin=log_size;
    return_requested();
    page(0x360);
#if COREMARK_SUITE
    for(unsigned bank=0;bank<2;bank++) {
        coremark_suite_page(suite_ticks,suite_valid,suite_crc,suite_phase,0);
        coremark_present();
    }
#else
    coremark_page(0,0);coremark_present();
#endif
    if(sizeof(ee_ptr_int)!=sizeof(void *) || sizeof(ee_u32)!=4)
        ee_printf("ERROR! Invalid RV32I port data types\n");
}
void portable_fini(core_portable *p) {
    p->portable_id=0;
    return_requested();
    /* Export exact report bytes via existing framebuffer DMA, outside timing. */
    int valid=0;
    for(ee_u32 i=phase_begin;i+17<log_size;i++) {
        const char *message="Correct operation validated.";ee_u32 j=0;
        while(message[j] && i+j<log_size && output[i+j]==message[j]) j++;
        if(!message[j]) valid=1;
    }
    valid=valid && !log_overflow && get_time()/platform_clock_hz>=10;
#if COREMARK_SUITE
    const core_results *result=(const core_results *)((const char *)p - offsetof(core_results,port));
    suite_crc[suite_phase][0]=result->crclist;suite_crc[suite_phase][1]=result->crcmatrix;suite_crc[suite_phase][2]=result->crcstate;
    suite_ticks[suite_phase]=get_time();suite_valid[suite_phase]=valid;
    return;
#else
    unsigned diagnostic=REG(0x1000011cu)&4u;
    result_page(valid,diagnostic);
    page(0x361);
    ee_u32 previous=REG(0x10000120u)&0xfffu;
    for(;;) {
        unsigned next_diagnostic=REG(0x1000011cu)&4u;
        if(next_diagnostic!=diagnostic){diagnostic=next_diagnostic;result_page(valid,diagnostic);}
        ee_u32 current=REG(0x10000120u)&0xfffu,pressed=current&~previous;previous=current;
        if(pressed&4u) { page(0x362);for(;;) {} }
        if(pressed&2u) { __asm__ volatile("j _start");__builtin_unreachable(); }
    }
#endif
}
#if COREMARK_SUITE
#undef main
#ifdef CORE_HOST
#define main core_suite_host_main
#endif
int main(void) {
    for(;;) {
        log_size=log_overflow=0;suite_details=0;
        suite_ticks[0]=suite_ticks[1]=0;suite_valid[0]=suite_valid[1]=0;
        for(suite_phase=0;suite_phase<2;suite_phase++) {
            seed1_volatile=seed2_volatile=suite_phase?0x3415:0;
            seed3_volatile=0x66;seed4_volatile=ITERATIONS;seed5_volatile=0;
            ee_printf("RVGAME CPU SUITE %s\n",suite_phase?"validation":"performance");
            coremark_run();
        }
        unsigned diagnostic=REG(0x1000011cu)&4u;
        result_page(0,diagnostic);page(0x361);
        ee_u32 previous=REG(0x10000120u)&0xfffu;
        for(;;) {
            unsigned next_diagnostic=REG(0x1000011cu)&4u;
            if(next_diagnostic!=diagnostic){diagnostic=next_diagnostic;result_page(0,diagnostic);}
            ee_u32 current=REG(0x10000120u)&0xfffu,pressed=current&~previous;previous=current;
            if(pressed&4u){page(0x362);for(;;){}}
            if(pressed&2u)break;
            if(pressed&256u){suite_details^=1;result_page(0,diagnostic);}
        }
    }
}
#endif
