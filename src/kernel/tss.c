#include "tss.h"

static TSS64 tss;

/* tss64_desc: dua qword (16 byte) di GDT, diisi oleh tss64_init().
 * Dideklarasikan global di kernel_entry.asm. */
extern uint8_t tss64_desc[];

void tss64_init(uint64_t kernel_stack) {
    int i;
    uint8_t *p = (uint8_t *)&tss;
    for (i = 0; i < (int)sizeof(TSS64); i++) p[i] = 0;

    tss.rsp0       = kernel_stack;
    tss.iomap_base = (uint16_t)sizeof(TSS64);

    /* Isi 16-byte GDT descriptor TSS64:
     * Bytes  0-1 : limit[15:0]
     * Bytes  2-4 : base[23:0]
     * Byte   5   : access = 0x89 (P=1, DPL=0, Type=9 = Available 64-bit TSS)
     * Byte   6   : 0x00  (limit/flags atas, G=0, limit fit dalam 16 bit)
     * Byte   7   : base[31:24]
     * Bytes  8-11: base[63:32]
     * Bytes 12-15: reserved = 0 */
    uint64_t base  = (uint64_t)&tss;
    uint32_t limit = (uint32_t)sizeof(TSS64) - 1;

    tss64_desc[0]  = (uint8_t)(limit & 0xFF);
    tss64_desc[1]  = (uint8_t)((limit >> 8) & 0xFF);
    tss64_desc[2]  = (uint8_t)(base & 0xFF);
    tss64_desc[3]  = (uint8_t)((base >> 8) & 0xFF);
    tss64_desc[4]  = (uint8_t)((base >> 16) & 0xFF);
    tss64_desc[5]  = 0x89;
    tss64_desc[6]  = 0x00;
    tss64_desc[7]  = (uint8_t)((base >> 24) & 0xFF);
    tss64_desc[8]  = (uint8_t)((base >> 32) & 0xFF);
    tss64_desc[9]  = (uint8_t)((base >> 40) & 0xFF);
    tss64_desc[10] = (uint8_t)((base >> 48) & 0xFF);
    tss64_desc[11] = (uint8_t)((base >> 56) & 0xFF);
    tss64_desc[12] = 0;
    tss64_desc[13] = 0;
    tss64_desc[14] = 0;
    tss64_desc[15] = 0;

    /* Load TSS register: selector 0x28 (GDT entry 5, 16-byte descriptor) */
    __asm__ volatile ("ltr %%ax" :: "a"((uint16_t)0x28));
}

void tss64_set_kernel_stack(uint64_t rsp) {
    tss.rsp0 = rsp;
}