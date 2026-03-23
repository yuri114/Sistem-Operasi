#include "pic.h"

/* Port I/O untuk PIC */
#define PIC1_COMMAND 0X20   /* Master PIC command port */
#define PIC1_DATA    0X21   /* Master PIC data port */
#define PIC2_COMMAND 0XA0   /* Slave PIC command port */
#define PIC2_DATA    0XA1   /* Slave PIC data port */

#define PIC_EOI      0x20   /* End-of-interrupt command code */

/* ICW1 - Initialization Command Word 1 */
#define ICW1_INIT    0x10   /* harus selalu di-set */
#define ICW1_ICW4    0x01   /* akan ada ICW4 */

/* ICW4 - Initialization Command Word 4 */
#define ICW4_8086    0x01   /* mode 8086 (bukan 8080) */

/* Fungsi bantu untuk I/O port */
static inline void outb(uint16_t port, uint8_t value){
    __asm__ volatile ("outb %0, %1":: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port){
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait(){
    /* tulis ke port 0x80 untuk kasih waktu PIC memproses */
    outb(0x80, 0);
}

void pic_init(){
    /* Simpan mask lama (opsional)*/

    /*Mulai sequence inisialisasi (ICW1)*/
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); /* Master PIC*/
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4); /* Slave PIC*/
    io_wait();
     
    /*  ICW2: set offset vector interrupt
        Master PIC -> IRQ0 mulai dari interrupt 32 (0x20)
        Slave PIC  -> IRQ8 mulai dari interrupt 40 (0x28) */
    outb(PIC1_DATA, 0x20); /* Master PIC offset */
    io_wait();
    outb(PIC2_DATA, 0x28); /* Slave PIC offset */
    io_wait();

    /*  ICW3: hubungan master dan slave
        Master: slave terhubung ke IRQ2 (bit 2 = 0b00000100)
        slave : identitas slave = 2*/
    outb(PIC1_DATA, 0x04); /* Master PIC: slave di IRQ2 */
    io_wait();
    outb(PIC2_DATA, 0x02); /* Slave PIC: identitas = 2 */
    io_wait();

    /* ICW4: mode 8086 */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Mask IRQ:
     * Master (IRQ0-7): hanya buka IRQ0 (timer=bit0) dan IRQ1 (keyboard=bit1).
     * IRQ2 (cascade slave) juga dibuka agar future slave IRQ bisa didaftarkan.
     * bit = 0 → unmask (aktif), bit = 1 → mask (non-aktif)
     * 0xF8 = 1111 1000: unmask IRQ0=0, IRQ1=1, IRQ2=2 */
    outb(PIC1_DATA, 0xF8); /* Master: aktifkan IRQ0 (timer), IRQ1 (kbd), IRQ2 (cascade) */
    outb(PIC2_DATA, 0xFF); /* Slave: semua di-mask (belum ada handler) */
}

void pic_send_eoi(uint8_t irq){
    /*Jika IRQ dari slave (IRQ8-IRQ15), kirim EOI ke slave dulu */
    if (irq >= 8) {
        /* Jika IRQ dari slave PIC, kirim EOI ke slave dulu */
        outb(PIC2_COMMAND, PIC_EOI);
    }
    /* Kirim EOI ke master PIC */
    outb(PIC1_COMMAND, PIC_EOI);
}