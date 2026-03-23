#ifndef VMM_H
#define VMM_H
#include "stdint.h"

// Ukuran halaman memori (4KB)
#define PAGE_SIZE 4096

void pmm_init(); // Inisialisasi Physical Memory Manager
uint32_t pmm_alloc_frame(); // Alokasikan satu halaman memori, kembalikan alamat fisik atau 0 jika gagal
void pmm_free_frame(uint32_t addr); // Bebaskan halaman memori yang dialokasikan
uint32_t* vmm_create_page_dir(); // Buat page directory baru untuk proses, kembalikan pointer atau 0 jika gagal
void vmm_switch_dir(uint32_t* page_dir); // Switch ke page directory baru (untuk context switch)
void vmm_map_page(uint32_t* page_dir, uint32_t virt, uint32_t phys, uint32_t flags); // Peta halaman virtual ke fisik
void vmm_free_user_memory(uint32_t *page_dir); // Bebaskan semua frame user milik proses ini

#endif