#include "idt.h"

/* Tabel IDT - 256 entries (0-255)*/
static idt_entry_t idt[256];
static idt_descriptor_t idt_desc;

/* Fungsi Assembly untuk load IDT ke CPU (dideklarasikan di isr.asm nanti)*/
extern void idt_load(idt_descriptor_t *desc);

/*set satu entry di IDT*/
void idt_set_gate(int n, uint32_t handler){
    idt[n].offset_low  = handler & 0xFFFF;          /* ambil 16 bit bawah*/
    idt[n].selector    = 0x08;                      /* code segment (dari GDT)*/
    idt[n].zero        = 0;                         /* selalu 0*/
    idt[n].type_attr   = 0x8E;                      /* 1000 1110: interrupt gate, ring 0*/
    idt[n].offset_high = (handler >> 16) & 0xFFFF;  /* ambil 16 bit atas*/
}

/* Inisialisasi IDT */
void idt_init(){
    int i;
    /* kosongkan semua 256 entry dulu*/
    for (i = 0; i < 256; i++)
    {
        idt_set_gate(i, 0);
    }
    
    /* Isi IDT descriptor*/
    idt_desc.limit = sizeof(idt) - 1;
    idt_desc.base  = (uint32_t) &idt;

    /* Load IDT ke CPU*/
    idt_load(&idt_desc);
}