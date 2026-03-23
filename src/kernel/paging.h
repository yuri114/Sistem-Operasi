#ifndef PAGING_H
#define PAGING_H
#include <stdint.h>

void paging_init();
void paging_map_vbe(uint32_t phys_addr); /* map 4MB LFB region ke virtual address sama */
uint32_t paging_get_cr0();
extern uint32_t page_directory[1024];

#endif