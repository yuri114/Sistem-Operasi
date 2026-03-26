/* idt.c -- Interrupt Descriptor Table (256 entry, 64-bit) */
#include "idt.h"

static idt_entry_t      idt[256];
static idt_descriptor_t idt_desc;

extern void idt_load(idt_descriptor_t *desc);

void idt_set_gate(int n, uint64_t handler) {
    idt[n].offset_lo  =  handler        & 0xFFFF;
    idt[n].selector   =  0x08;
    idt[n].ist        =  0;
    idt[n].type_attr  =  0x8E;
    idt[n].offset_mid = (handler >> 16) & 0xFFFF;
    idt[n].offset_hi  = (handler >> 32) & 0xFFFFFFFF;
    idt[n].reserved   =  0;
}

void idt_init() {
    int i;
    for (i = 0; i < 256; i++)
        idt_set_gate(i, 0);
    idt_desc.limit = sizeof(idt) - 1;
    idt_desc.base  = (uint64_t)&idt;
    idt_load(&idt_desc);
}

void idt_set_gate_user(int n, uint64_t handler) {
    idt_set_gate(n, handler);
    idt[n].type_attr = 0xEE;
}