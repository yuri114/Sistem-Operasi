#ifndef TIMER_H
#define TIMER_H
#include <stdint.h>

#define TIMER_HZ    1000    /* PIT di-set 1000 Hz = 1ms per tick */

void timer_init(uint32_t frequency);
uint32_t get_ticks();              /* jumlah ms sejak boot   */
void msleep_kernel(uint32_t ms);   /* busy-sleep di kernel   */

#endif