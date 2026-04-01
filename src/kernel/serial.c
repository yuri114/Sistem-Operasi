/* serial.c — Driver Serial COM1 (0x3F8) untuk debug output
 * QEMU: tambahkan -serial stdio pada command line untuk membaca output ini.
 */
#include <stdint.h>
#include "serial.h"

#define COM1_BASE   0x3F8

#define COM1_DATA   (COM1_BASE + 0)   /* Data register           */
#define COM1_IER    (COM1_BASE + 1)   /* Interrupt Enable        */
#define COM1_FCR    (COM1_BASE + 2)   /* FIFO Control Register   */
#define COM1_LCR    (COM1_BASE + 3)   /* Line Control Register   */
#define COM1_MCR    (COM1_BASE + 4)   /* Modem Control Register  */
#define COM1_LSR    (COM1_BASE + 5)   /* Line Status Register    */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* Inisialisasi COM1: 9600 baud, 8N1, no IRQ. */
void serial_init() {
    outb(COM1_IER, 0x00);   /* Matikan semua interrupt            */
    outb(COM1_LCR, 0x80);   /* Enable DLAB (set baud rate divisor)*/
    outb(COM1_DATA, 0x0C);  /* Divisor low byte: 12 → 9600 baud  */
    outb(COM1_IER, 0x00);   /* Divisor high byte: 0               */
    outb(COM1_LCR, 0x03);   /* 8-bit, no parity, 1 stop bit       */
    outb(COM1_FCR, 0xC7);   /* FIFO on, clear, threshold 14 byte  */
    outb(COM1_MCR, 0x0B);   /* IRQs on, RTS/DSR on                */
}

/* Tunggu sampai transmit buffer kosong lalu kirim satu byte. */
void serial_putchar(char c) {
    int timeout = 0x10000;
    while (!(inb(COM1_LSR) & 0x20) && timeout--);
    if (c == '\n') {
        /* Kirim CR+LF agar terbaca lebih jelas di terminal host */
        while (!(inb(COM1_LSR) & 0x20));
        outb(COM1_DATA, '\r');
    }
    while (!(inb(COM1_LSR) & 0x20));
    outb(COM1_DATA, (uint8_t)c);
}

/* Kirim string ke COM1. */
void serial_print(const char *s) {
    while (*s) serial_putchar(*s++);
}

/* Kirim nilai hex 64-bit ke COM1. */
void serial_print_hex(unsigned long long v) {
    const char *hex = "0123456789ABCDEF";
    int i;
    serial_print("0x");
    for (i = 60; i >= 0; i -= 4)
        serial_putchar(hex[(v >> i) & 0xF]);
}
