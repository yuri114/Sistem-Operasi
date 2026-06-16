#ifndef VMM_H
#define VMM_H
#include <stdint.h>

#define PAGE_SIZE 4096

/* PTE flag: bit 9 dipakai sebagai COW (Copy-on-Write) marker */
#define PTE_COW  (1ULL << 9)

void     pmm_init();
uint64_t pmm_alloc_frame();
void     pmm_free_frame(uint64_t addr);
uint32_t pmm_free_count();   /* jumlah frame bebas saat ini */

/* Aman dipanggil saat CR3 = process page dir (switch ke boot PML4 secara internal) */
void     vmm_zero_frame(uint64_t phys);
void     vmm_copy_to_frame(uint64_t phys, uint64_t off,
                            const uint8_t *src, uint64_t len);

uint64_t *vmm_create_page_dir();
void      vmm_switch_dir(uint64_t *pml4);
void      vmm_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void      vmm_unmap_page(uint64_t *pml4, uint64_t virt);   /* hapus pemetaan 1 halaman */
void      vmm_free_user_memory(uint64_t *pml4);

/* F-Q: Copy-on-Write dan helper */
uint64_t  vmm_get_phys(uint64_t *pml4, uint64_t virt);     /* physical addr from VA, 0=not mapped */
uint64_t *vmm_get_pte(uint64_t *pml4, uint64_t virt);      /* pointer ke PTE, NULL=no PT */
void      vmm_copy_cow(uint64_t *parent_pml4, uint64_t *child_pml4); /* fork COW setup */
int       vmm_cow_fault(uint64_t *pml4, uint64_t virt);    /* handle write fault; 1=handled */

#endif