/* idt.c — Interrupt Descriptor Table (256 entry, 32-bit interrupt gates) */
#include "idt.h"

static idt_entry_t     idt[256];
static idt_descriptor_t idt_desc;

extern void idt_load(idt_descriptor_t *desc);  /* implementasi di isr.asm */

/* -------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------- */

/*
 * Pasang handler di gate n sebagai interrupt gate ring-0 (type_attr = 0x8E).
 * Semua interrupt kernel menggunakan selector code segment 0x08.
 */
void idt_set_gate(int n, uint32_t handler) {
    idt[n].offset_low  =  handler        & 0xFFFF;
    idt[n].selector    =  0x08;
    idt[n].zero        =  0;
    idt[n].type_attr   =  0x8E;          /* P=1 DPL=0 Type=interrupt gate */
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

/* Inisialisasi dan muat IDT ke CPU. Semua gate dikosongkan terlebih dahulu. */
void idt_init() {
    int i;
    for (i = 0; i < 256; i++)
        idt_set_gate(i, 0);

    idt_desc.limit = sizeof(idt) - 1;
    idt_desc.base  = (uint32_t)&idt;
    idt_load(&idt_desc);
}

/* Sama seperti idt_set_gate, tapi DPL=3 agar ring-3 bisa memanggil (mis. int 0x80). */
void idt_set_gate_user(int n, uint32_t handler) {
    idt_set_gate(n, handler);
    idt[n].type_attr = 0xEE;             /* P=1 DPL=3 Type=interrupt gate */
}