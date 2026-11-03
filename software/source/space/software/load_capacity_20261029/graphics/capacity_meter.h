/* Application-side, all-frame metering. PS reads the frozen export only.
 * Bins: 32768 cycles (0.312076 ms), last bin is overflow. No per-frame log RAM.
 * Unsigned deltas accumulate into 64 bits across the 40.9-second tick wrap.
 */
static uint32_t cap_words[256];
static uint32_t cap_previous_tick,cap_previous_scan,cap_previous_completed;
static unsigned cap_active,cap_frozen,cap_have_previous;
static void cap_config(unsigned count,unsigned sw,unsigned pair,unsigned opacity){
    cap_words[34]=count;cap_words[35]=sw;cap_words[36]=pair;cap_words[37]=opacity;
}
static void cap_reset(void){
    for(unsigned i=0;i<256;i++)cap_words[i]=0;
    cap_words[0]=0x43415031u;cap_words[1]=1;
    cap_active=1;cap_frozen=cap_have_previous=0;
    cap_previous_tick=DRAW(80);cap_previous_scan=STATE(24);cap_previous_completed=DRAW(28);
}
static void cap_add64(unsigned i,uint32_t delta){
    uint32_t old=cap_words[i];cap_words[i]+=delta;if(cap_words[i]<old)cap_words[i+1]++;
}
static void cap_hist(unsigned offset,uint32_t cycles){
    unsigned bin=cycles>>15;if(bin>63)bin=63;cap_words[offset+bin]++;
}
static void cap_work(uint32_t work,uint32_t draw,uint32_t phase){
    if(!cap_active)return;
    cap_words[2]++;cap_words[15]=phase;
#ifndef WORK_HOST
    uint32_t stack;__asm__ volatile("mv %0,sp":"=r"(stack));
    if(!cap_words[38] || stack<cap_words[38])cap_words[38]=stack;
#endif
    cap_words[16]=(cap_words[16]^phase)*16777619u;
    cap_add64(6,draw);cap_add64(8,work);
    if(draw>cap_words[10])cap_words[10]=draw;
    if(work>cap_words[11])cap_words[11]=work;
    if(work>1750000u)cap_words[12]++;
    cap_hist(64,draw);cap_hist(128,work);
}
static void cap_present(void){
    if(!cap_active)return;
    uint32_t now=DRAW(80),scan=STATE(24),completed=DRAW(28);
    if(cap_have_previous){
        uint32_t dt=now-cap_previous_tick,ds=scan-cap_previous_scan;
        cap_add64(3,dt);cap_words[5]+=ds;cap_words[13]+=completed-cap_previous_completed;
        if(ds!=1){cap_words[14]++;if(ds>1)cap_words[17]+=ds-1;}
        cap_hist(192,dt);if(dt>cap_words[18])cap_words[18]=dt;
        if(dt>1750000u+32768u)cap_words[19]++;
    }
    cap_previous_tick=now;cap_previous_scan=scan;cap_previous_completed=completed;cap_have_previous=1;
}
static int cap_control(unsigned pressed,unsigned phase){
    if((STATE(28)&4u) && (pressed&256u)){cap_reset();cap_words[20]=phase;}
    if((STATE(28)&4u) && (pressed&2048u) && cap_active){cap_active=0;cap_frozen=1;}
    if(!cap_frozen)return 0;
    cap_words[21]=STATE(24);cap_words[22]=DRAW(28);cap_words[23]=DRAW(16);
    cap_words[24]=cap_words[2]^0xffffffffu;
    game_wait();DRAW(72)=640u*480u-sizeof(cap_words);
    for(unsigned i=0;i<256;i++)for(unsigned b=0;b<4;b++)DRAW(76)=(cap_words[i]>>(b*8))&255u;
    game_wait();DRAW(12)=2;game_wait();return 1;
}
