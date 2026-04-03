/* rtc.c — CMOS RTC driver untuk x86.
 * Membaca register CMOS via port I/O 0x70 (index) dan 0x71 (data).
 * Konversi BCD→binary secara otomatis.
 * QEMU mensimulasikan RTC yang mengikuti jam host. */
#include "rtc.h"
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0,%1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1,%0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Baca satu register CMOS (pastikan NMI masih aktif: bit 7 = 0). */
static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg & 0x7F);
    /* Tunggu sebentar agar CMOS stabil */
    uint32_t i; for (i = 0; i < 100; i++) __asm__ volatile ("nop");
    return inb(0x71);
}

/* BCD → binary */
static uint8_t bcd2bin(uint8_t bcd) {
    return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

/* Tunggu sampai RTC selesai update (bit 7 Status Register A). */
static void rtc_wait_ready(void) {
    while (cmos_read(0x0A) & 0x80);
}

void rtc_read(RtcTime *t) {
    uint8_t regB;
    rtc_wait_ready();

    t->second = cmos_read(0x00);
    t->minute = cmos_read(0x02);
    t->hour   = cmos_read(0x04);
    t->day    = cmos_read(0x07);
    t->month  = cmos_read(0x08);
    uint8_t yr = cmos_read(0x09);
    /* Century register (0x32) tersedia di sebagian besar BIOS/QEMU */
    uint8_t cent = cmos_read(0x32);

    regB = cmos_read(0x0B);

    /* Konversi BCD → binary jika bit 2 Status B tidak set */
    if (!(regB & 0x04)) {
        t->second = bcd2bin(t->second);
        t->minute = bcd2bin(t->minute);
        t->hour   = bcd2bin(t->hour & 0x7F);  /* mask AM/PM bit */
        t->day    = bcd2bin(t->day);
        t->month  = bcd2bin(t->month);
        yr        = bcd2bin(yr);
        cent      = bcd2bin(cent);
    }

    /* Konversi 12-jam ke 24-jam jika perlu */
    if (!(regB & 0x02) && (t->hour & 0x80)) {
        t->hour = (uint8_t)(((t->hour & 0x7F) + 12) % 24);
    }

    /* Hitung tahun lengkap */
    if (cent != 0)
        t->year = (uint16_t)(cent * 100 + yr);
    else
        t->year = (uint16_t)((yr < 70 ? 2000 : 1900) + yr);
}
