#include "paging.h"

uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_table[1024]     __attribute__((aligned(4096)));
/* Page tables tambahan untuk identity-map 4MB–16MB (dipakai PMM untuk frame 1024–4095) */
static uint32_t page_table1[1024]    __attribute__((aligned(4096)));
static uint32_t page_table2[1024]    __attribute__((aligned(4096)));
static uint32_t page_table3[1024]    __attribute__((aligned(4096)));
/* Page table khusus untuk VBE LFB (4MB region di 0xE0000000) */
static uint32_t vbe_page_table[1024] __attribute__((aligned(4096)));

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
        page_table[i] = (i * 0x1000) | 7; // present + read/write + user (bit 2) agar ring 3 bisa akses
    }
    /* 4MB–8MB (pd_idx=1) — kernel-only (flags=3, tidak user) */
    for (i = 0; i < 1024; i++)
        page_table1[i] = (0x400000u + (uint32_t)(i * 0x1000)) | 3;
    /* 8MB–12MB (pd_idx=2) */
    for (i = 0; i < 1024; i++)
        page_table2[i] = (0x800000u + (uint32_t)(i * 0x1000)) | 3;
    /* 12MB–16MB (pd_idx=3) */
    for (i = 0; i < 1024; i++)
        page_table3[i] = (0xC00000u + (uint32_t)(i * 0x1000)) | 3;

    /*  isi page_directory: semua kosong dulu*/
    for (i = 0; i < 1024; i++) {
        page_directory[i] = 2;
    }
    /*  pasang page_table ke page_directory */
    page_directory[0] = ((uint32_t)page_table)  | 7; // present + read/write + user
    page_directory[1] = ((uint32_t)page_table1) | 3; // kernel-only
    page_directory[2] = ((uint32_t)page_table2) | 3;
    page_directory[3] = ((uint32_t)page_table3) | 3;

    /* aktifkan paging */
    load_page_directory(page_directory);
    enable_paging();
}

uint32_t paging_get_cr0() {
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}

/* Petakan 4MB region di phys_addr ke virtual address yang sama (identity map).
 * Digunakan untuk mengakses VBE LFB QEMU di 0xE0000000 setelah paging aktif.
 * Flush TLB setelah modifikasi page directory. */
void paging_map_vbe(uint32_t phys_addr) {
    uint32_t pd_index = phys_addr >> 22;  /* bits [31:22] */
    int i;
    for (i = 0; i < 1024; i++)
        vbe_page_table[i] = (phys_addr + (uint32_t)(i * 0x1000)) | 3; /* present + rw */
    page_directory[pd_index] = ((uint32_t)vbe_page_table) | 3;
    /* Flush TLB dengan reload CR3 */
    __asm__ volatile (
        "mov %%cr3, %%eax\n"
        "mov %%eax, %%cr3\n"
        ::: "eax"
    );
}