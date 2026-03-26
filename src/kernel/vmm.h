#ifndef VMM_H
#define VMM_H
#include <stdint.h>

#define PAGE_SIZE 4096

void     pmm_init();
uint64_t pmm_alloc_frame();
void     pmm_free_frame(uint64_t addr);

uint64_t *vmm_create_page_dir();
void      vmm_switch_dir(uint64_t *pml4);
void      vmm_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void      vmm_free_user_memory(uint64_t *pml4);

#endif