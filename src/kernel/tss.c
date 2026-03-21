#include "tss.h"

static TSS tss; //struktur TSS yang akan digunakan 

//GDT entry 5 (TSS) ada di kernel_entry.asm
extern uint8_t tss_desc[]; //alamat tss_desc di GDT

void tss_init(uint32_t kernel_stack) {
    //zero semua field TSS
    int i;
    uint8_t *p = (uint8_t*)&tss;
    for (i = 0; i < sizeof(TSS); i++) {
        p[i] = 0;
    }
    tss.ss0 = 0x10; //segment selector untuk stack ring 0 (kernel), ini yang di-pop oleh "pop eax" di irq0
    tss.esp0 = kernel_stack; //stack pointer untuk ring 0 (kernel), ini yang di-load ke esp saat switch ke ring 0
    tss.iomap_base = sizeof(TSS); //tidak ada bitmap I/O, jadi letakkan di akhir TSS

    //isi GDT entry 5 dengan info TSS
    uint32_t base = (uint32_t)&tss;
    uint32_t limit = sizeof(TSS) - 1;

    tss_desc[0] = limit & 0xFF; //limit byte 0-7
    tss_desc[1] = (limit >> 8) & 0xFF; //limit byte 8-15
    tss_desc[2] = base & 0xFF; //base byte 0-7
    tss_desc[3] = (base >> 8) & 0xFF; //base byte 8-15
    tss_desc[4] = (base >> 16) & 0xFF; //base byte 16-23
    tss_desc[5] = 0x89; //type=9 (available 32-bit TSS), S=0 (system), DPL=0, P=1
    tss_desc[6] = 0x00; //limit byte 16-19 (tidak digunakan karena limit < 64KB)
    tss_desc[7] = (base >> 24) & 0xFF; //base byte 24-31

    //load TTS ke CPU (selector 0x28 = entry 5 di GDT)
    __asm__ volatile ("ltr %%ax" :: "a"((uint16_t)0x28));
}

void tss_set_kernel_stack(uint32_t esp) {
    tss.esp0 = esp; //update stack pointer untuk ring 0 (kernel)
}