#include "timer.h"
#include "task.h"
#define PIT_CHANNEL0 0x40  // port data channel 0
#define PIT_COMMAND  0x43  //Port command
#define PIT_FREQUENCY 1193180 //frekuensi dasar PIT dalam Hz
static uint32_t tick = 0; //counter untuk jumlah ticks sejak timer diinisialisasi

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1":: "a"(value), "Nd"(port));
}

void timer_handler() {
    tick++; //naikkan counter setiap timer interrupt
    task_check_sleepers(); //bangunkan task sleeping yang waktunya habis
}

void timer_init(uint32_t frequency) {
    uint32_t divisor = PIT_FREQUENCY / frequency; //hitung divisor untuk frekuensi yang diinginkan
    
    outb(PIT_COMMAND, 0x36); //mode 3 (square wave), akses byte rendah dan tinggi

    outb(PIT_CHANNEL0, divisor & 0xFF); //kirim byte rendah
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF); //kirim byte tinggi
}

uint32_t get_ticks() {
    return tick; //kembalikan jumlah ticks sejak timer diinisialisasi
}