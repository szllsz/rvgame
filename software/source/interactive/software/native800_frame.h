#ifndef NATIVE800_FRAME_H
#define NATIVE800_FRAME_H
#include <stdint.h>
#ifdef NATIVE800_HOST
uint32_t *native800_host_reg(unsigned offset);
uint32_t *native800_host_state(unsigned offset);
#endif
int native800_begin(void);
void native800_band(unsigned band);
void native800_flush(unsigned band);
void native800_wait(void);
void native800_offset(int x,int y);
void native800_layout640(unsigned enabled);
int native800_layout_coordinate(int coordinate);
void native800_clip(int l,int t,int r,int b);
void native800_clear(unsigned color);
void native800_fill(int x,int y,int w,int h,unsigned color);
void native800_tile(int x,int y,unsigned tile,unsigned palette,unsigned fx,unsigned fy);
void native800_bitmap(const uint8_t *source,unsigned stride,unsigned w,unsigned h,
                      int x,int y,unsigned fx,unsigned fy,unsigned key,unsigned alpha);
void native800_bitmap_physical(const uint8_t *source,unsigned stride,unsigned w,unsigned h,
                              int x,int y,unsigned fx,unsigned fy,unsigned key,unsigned alpha);
void native800_pixel(unsigned x,unsigned y,unsigned color);
void native800_pixel_linear(unsigned address,unsigned color);
unsigned native800_top(void);
void native800_abort(void);
unsigned native800_active(void);
#endif
