#ifndef PAGING_H
#define PAGING_H
#include <stdint.h>

/* Paging 64-bit Long Mode.
 * 4-level page tables disiapkan oleh kernel_entry.asm di 0x1000-0x6FFF.
 * paging_init() dan paging_map_vbe() adalah no-op di 64-bit. */

void paging_init();
void paging_map_vbe(uint32_t phys_addr);
uint32_t paging_get_cr0();

#endif