#ifndef IDT_H
#define IDT_H
#include <stdint.h>

/* IDT gate 64-bit: 16 byte */
typedef struct {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

/* Pointer untuk lidt */
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_descriptor_t;

void idt_init();
void idt_set_gate(int n, uint64_t handler);
void idt_set_gate_user(int n, uint64_t handler);
#endif