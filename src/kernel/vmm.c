#include "vmm.h"
#include "memory.h"

// 16MB / 4KB = 4096 frame, butuh 4096/8 = 512 uint32_t untuk bitmap
#define TOTAL_FRAMES 4096
static uint8_t frame_bitmap[TOTAL_FRAMES / 8]; //bitmap untuk melacak frame yang digunakan

static void bitmap_set(uint32_t frame) {
    frame_bitmap[frame / 8 ] |= (1 << (frame % 8)); //set bit untuk frame ini
}

static void bitmap_clear(uint32_t frame) {
    frame_bitmap[frame / 8 ] &= ~(1 << (frame % 8)); //clear bit untuk frame ini
}

static int bitmap_test(uint32_t frame) {
    return frame_bitmap[frame / 8 ] & (1 << (frame % 8)); //cek apakah bit untuk frame ini sudah diset
}

void pmm_init() {
    int i;
    //tandai semua frame sebagai bebas (clear bitmap)
    for(i=0; i < TOTAL_FRAMES / 8; i++) {
        frame_bitmap[i] = 0;
    }
    // tandai 4MB pertama (1024 frame) sebagai sudah dipakai
    // karena disitu ada kernel, stack, page tables, dll
    for (i=0; i < 768 ; i++) {
        bitmap_set(i);
    }
}

uint32_t pmm_alloc_frame() {
    int i;
    for(i = 768; i < 1024; i++) { //mulai dari frame 768 karena 0-767 sudah dipakai
        if (!bitmap_test(i)) { //cari frame yang bebas
            bitmap_set(i); //tandai frame ini sebagai dipakai
            return (uint32_t)(i * PAGE_SIZE); //kembalikan alamat fisik frame ini
        }
    }
    return 0; // tidak ada frame yang tersedia
}

void pmm_free_frame(uint32_t addr) {
    bitmap_clear(addr / PAGE_SIZE); //bebaskan frame dengan clear bit di bitmap
}

// Virtual Memory Manager

void vmm_switch_dir(uint32_t* page_dir) {
    __asm__ volatile ("mov %0, %%cr3":: "r"(page_dir)); //switch page directory dengan load CR3
}

void vmm_map_page(uint32_t* page_dir, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_idx = virt >> 22; //10 bit untuk index page directory
    uint32_t pt_idx = (virt >> 12) & 0x3FF; //10 bit untuk index page table

    uint32_t *page_table;
    if (page_dir[pd_idx] & 1) { //jika page table sudah ada
        page_table = (uint32_t*)(page_dir[pd_idx] & 0xFFFFF000); //ambil alamat page table dari entry
    }
    else { //jika belum ada, buat baru
        page_table = (uint32_t*)malloc(PAGE_SIZE); //alokasikan frame untuk page table baru
        int i;
        for (i = 0; i < 1024; i++)
        {
            page_table[i] = 0; //inisialisasi semua entry page table sebagai tidak valid
        }
        page_dir[pd_idx] = ((uint32_t)page_table) | flags | 1; //set entry di page directory dengan alamat page table + flag present
    }
    page_table[pt_idx] = (phys & 0xFFFFF000) | flags | 1; //set entry di page table dengan alamat fisik + flag present
}

uint32_t* vmm_create_page_dir() {
    uint32_t *dir = (uint32_t*)malloc(PAGE_SIZE); //alokasikan frame untuk page directory baru
    int i;
    for (i = 0; i < 1024; i++) {
        dir[i] = 0; //inisialisasi semua entry page directory sebagai tidak valid
    }
    extern uint32_t page_directory[]; //gunakan page directory kernel yang sudah ada untuk mapping 4MB pertama

    // deep-copy page table kernel (bukan copy pointer) agar tiap proses punya page table sendiri
    // sehingga vmm_map_page tidak merusak mapping kernel
    if (page_directory[0] & 1) {
        uint32_t *kernel_pt = (uint32_t*)(page_directory[0] & 0xFFFFF000);
        uint32_t *new_pt = (uint32_t*)malloc(PAGE_SIZE);
        for (i = 0; i < 1024; i++) new_pt[i] = kernel_pt[i];
        dir[0] = ((uint32_t)new_pt) | (page_directory[0] & 0xFFF); //same flags, new page table
    }

    return dir;
}

