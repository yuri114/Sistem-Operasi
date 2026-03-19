#include "paging.h"

static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_table[1024]     __attribute__((aligned(4096)));

static void load_page_directory(uint32_t *dir) {
    __asm__ volatile ("mov %0, %%cr3":: "r"(dir));
}

static void enable_paging() {
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; //set bit 31 untuk enable paging
    __asm__ volatile ("mov %0, %%cr0":: "r"(cr0));
}

void paging_init() {
    int i;
    /*  isi page_table: petakan 1024 halaman (= 4MB pertama)
        setiap entry = alamat fisik halaman | flag 
        flag 3 bit0 = present, bit1 = read/write*/
    for (i = 0; i < 1024; i++) {
        page_table[i] = (i * 0x1000) | 3;
    }
    /*  isi page_directory: semua kosong dulu*/
    for (i = 0; i < 1024; i++) {
        page_directory[i] = 2;
    }
    /*  pasang page_table pertama ke page_directory */
    page_directory[0] = ((uint32_t)page_table) | 3; //present + read/write

    /* aktifkan paging */
    load_page_directory(page_directory);
    enable_paging();
}

uint32_t paging_get_cr0() {
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}