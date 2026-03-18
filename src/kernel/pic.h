#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/*  Inisialisasi dan remap PIC:
    IRQ0-7 -> interrupt vector 32-39
    IRQ8-15 -> interrupt vector 40-47*/
void pic_init();

/* Kirim End-Of-Interrupt ke PIC setelah menangani IRQ*/
void pic_send_eoi(uint8_t irq);

#endif