#ifndef TSS_H
#define TSS_H
#include <stdint.h>

/* TSS minimalnya untuk 64-bit Long Mode
 * Hanya rsp0 yang dipakai (stack ring-0 saat ring-3?ring-0 transition). */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;          /* kernel stack pointer untuk ring-3 -> ring-0 */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];        /* interrupt stack table (tidak dipakai, 0 semua) */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;    /* offset I/O bitmap (= sizeof TSS = no bitmap) */
} __attribute__((packed)) TSS64;

void tss64_init(uint64_t kernel_stack);
void tss64_set_kernel_stack(uint64_t rsp);
#endif