#ifndef TIMER_H
#define TIMER_H
#include <stdint.h>

void timer_init(uint32_t frequency); /* inisialisasi PIT */
uint32_t get_ticks();                /* ambil nilai counter */

#endif