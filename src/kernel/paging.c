/* paging.c ? 64-bit Long Mode: page tables disiapkan oleh kernel_entry.asm.
 * Identity map 4GB sudah aktif saat kernel_main() dipanggil, termasuk VBE LFB.
 * paging_init() dan paging_map_vbe() menjadi no-op. */
#include "paging.h"

void paging_init() {
    /* No-op: 4-level paging sudah aktif dari kernel_entry.asm */
}

void paging_map_vbe(uint32_t phys_addr) {
    (void)phys_addr;
    /* No-op: VBE LFB sudah di identity-map dalam PD[3-4GB] di kernel_entry.asm */
}

uint32_t paging_get_cr0() {
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return (uint32_t)cr0;
}