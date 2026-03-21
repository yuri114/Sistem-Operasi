#ifndef TSS_H
#define TSS_H
#include <stdint.h>

typedef struct{
    uint32_t prev_tss; //tidak digunakan, harus 0
    uint32_t esp0; //stack pointer untuk ring 0 (kernel)
    uint32_t ss0; //segment selector untuk stack ring 0
    uint32_t esp1, ss1, esp2, ss2; //tidak digunakan, harus 0
    uint32_t cr3, eip, eflags; //tidak digunakan, harus 0
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi; //tidak digunakan, harus 0
    uint32_t es, cs, ss, ds, fs, gs; //tidak digunakan, harus 0
    uint32_t ldt; //tidak digunakan, harus 0
    uint16_t trap, iomap_base; //tidak digunakan, harus 0
} __attribute__((packed)) TSS;

void tss_init(uint32_t kernel_stack);
void tss_set_kernel_stack(uint32_t esp);
#endif