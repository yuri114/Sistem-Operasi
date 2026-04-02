/* timer.c — Programmable Interval Timer (PIT 8253/8254) @ 1000Hz (1ms/tick) */
#include "timer.h"
#include "task.h"
#include "net.h"

#define PIT_CHANNEL0    0x40    /* port data channel 0 */
#define PIT_COMMAND     0x43    /* port command */
#define PIT_BASE_HZ     1193180 /* frekuensi osilator PIT */

static volatile uint32_t tick = 0;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

/* -------------------------------------------------------------------
 * Handler & API
 * ------------------------------------------------------------------- */

/* Dipanggil per-IRQ0: naikkan counter dan bangunkan task yang selesai sleep. */
void timer_handler() {
    tick++;
    task_check_sleepers();
    /* F-X1/X3: TCP retransmit + keepalive timer setiap 10 ms */
    if (tick % 10 == 0)
        net_tcp_tick();
}

/*
 * Atur PIT agar menghasilkan IRQ0 sebanyak `frequency` kali per detik.
 * Mode 3 (square wave generator), akses low byte + high byte.
 */
void timer_init(uint32_t frequency) {
    uint32_t divisor = PIT_BASE_HZ / frequency;
    outb(PIT_COMMAND,  0x36);                       /* mode 3, LSB+MSB */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));  /* divisor low byte */
    outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));    /* divisor high byte */
}

/* Kembalikan jumlah tick sejak boot (1 tick = 1ms pada 1000 Hz). */
uint32_t get_ticks() {
    return tick;
}

/* Tunggu setidaknya `ms` milidetik (busy-wait, hanya untuk kernel). */
void msleep_kernel(uint32_t ms) {
    uint32_t end = tick + ms;
    while (tick < end)
        __asm__ volatile ("pause");
}