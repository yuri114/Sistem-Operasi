#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/*satu entry di IDT = 8 byte*/
typedef struct{            /* IDT entry untuk interrupt gate*/
    uint16_t offset_low;   /* alamat handler [0:15]*/ 
    uint16_t selector;     /* code segment selector (0x08)*/
    uint8_t zero;          /* selalu 0*/
    uint8_t type_attr;     /* tipe gate + privilege*/
    uint16_t offset_high;  /* alamat handler [16:31]*/
} __attribute__((packed)) idt_entry_t;/*__attribute__((packed)) untuk memastikan compiler tidak menambahkan padding di antara field-field struct*/

/*struktur yang di load ke CPU via 'lidt'*/
typedef struct{
    uint16_t limit;        /* ukuran IDT - 1*/
    uint32_t base;         /* alamat IDT di memory*/
} __attribute__((packed)) idt_descriptor_t;/*__attribute__((packed)) untuk memastikan compiler tidak menambahkan padding di antara field-field struct*/

void idt_init();
void idt_set_gate(int n,uint32_t handler);

#endif
