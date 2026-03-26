/* pic.c — Inisialisasi 8259A Programmable Interrupt Controller (PIC) */
#include "pic.h"

/* -------------------------------------------------------------------
 * Port I/O PIC
 * ------------------------------------------------------------------- */
#define PIC1_COMMAND  0x20   /* master: command */
#define PIC1_DATA     0x21   /* master: data / IMR */
#define PIC2_COMMAND  0xA0   /* slave: command */
#define PIC2_DATA     0xA1   /* slave: data / IMR */
#define PIC_EOI       0x20   /* End-of-Interrupt */

/* ICW1 */
#define ICW1_INIT     0x10   /* bit wajib */
#define ICW1_ICW4     0x01   /* akan diikuti ICW4 */
/* ICW4 */
#define ICW4_8086     0x01   /* mode 8086 (bukan 8080) */

/* -------------------------------------------------------------------
 * Helper I/O
 * ------------------------------------------------------------------- */
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void io_wait() { outb(0x80, 0); }  /* beri waktu PIC memproses */

/* -------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------- */

/*
 * Inisialisasi ulang kedua PIC dengan ICW1–ICW4.
 *
 * Vektor IRQ setelah remapping:
 *   Master IRQ0–7  → INT 32–39 (0x20–0x27)
 *   Slave  IRQ8–15 → INT 40–47 (0x28–0x2F)
 *
 * IMR setelah init:
 *   Master 0xF8 = unmask IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade)
 *   Slave  0xFF = semua di-mask (handler didaftarkan via mouse_init, dll.)
 */
void pic_init() {
    /* ICW1: mulai sequence inisialisasi */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();

    /* ICW2: offset vektor */
    outb(PIC1_DATA, 0x20); io_wait();   /* master: IRQ0 → INT 32 */
    outb(PIC2_DATA, 0x28); io_wait();   /* slave:  IRQ8 → INT 40 */

    /* ICW3: hubungan master–slave */
    outb(PIC1_DATA, 0x04); io_wait();   /* master: slave di IRQ2 (bit 2) */
    outb(PIC2_DATA, 0x02); io_wait();   /* slave: cascade identity = 2 */

    /* ICW4: mode 8086 */
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    /* IMR: aktifkan hanya IRQ0, IRQ1, IRQ2 di master */
    outb(PIC1_DATA, 0xF8);
    outb(PIC2_DATA, 0xFF);
}

/* Kirim EOI ke master (dan slave jika IRQ ≥ 8). */
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_COMMAND, PIC_EOI);
    outb(PIC1_COMMAND, PIC_EOI);
}