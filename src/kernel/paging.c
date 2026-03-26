/* paging.c — Inisialisasi paging 32-bit flat (identity map 0–16MB + VBE LFB) */
#include "paging.h"

/* -------------------------------------------------------------------
 * Page tables (aligned 4KB, di BSS — di-zero oleh kernel_entry.asm)
 *
 *   PD[0]  → page_table   : 0MB–4MB   (P+RW+User, agar ring-3 bisa akses)
 *   PD[1]  → page_table1  : 4MB–8MB   (P+RW, kernel-only)
 *   PD[2]  → page_table2  : 8MB–12MB  (P+RW, kernel-only)
 *   PD[3]  → page_table3  : 12MB–16MB (P+RW, kernel-only)
 *   PD[N]  → vbe_page_table: VBE LFB   (dipasang oleh paging_map_vbe)
 * ------------------------------------------------------------------- */
uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_table[1024]     __attribute__((aligned(4096)));
static uint32_t page_table1[1024]    __attribute__((aligned(4096)));
static uint32_t page_table2[1024]    __attribute__((aligned(4096)));
static uint32_t page_table3[1024]    __attribute__((aligned(4096)));
static uint32_t vbe_page_table[1024]  __attribute__((aligned(4096)));  /* LFB block 0: 4MB */
static uint32_t vbe_page_table2[1024] __attribute__((aligned(4096)));  /* LFB block 1: next 4MB */

/* -------------------------------------------------------------------
 * Helper internal
 * ------------------------------------------------------------------- */

static void load_page_directory(uint32_t *dir) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(dir));
}

static void enable_paging() {
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  /* set bit PE */
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0));
}

/* -------------------------------------------------------------------
 * API publik
 * ------------------------------------------------------------------- */

/*
 * Siapkan identity map 0–16MB dan aktifkan paging.
 * Ring-3 hanya bisa akses 0–4MB (PD[0] flags=7 = P+RW+User).
 */
void paging_init() {
    int i;

    /* 0MB–4MB: P+RW+User */
    for (i = 0; i < 1024; i++)
        page_table[i] = (i * 0x1000) | 7;

    /* 4MB–8MB: P+RW kernel-only */
    for (i = 0; i < 1024; i++)
        page_table1[i] = (0x400000u + (uint32_t)(i * 0x1000)) | 3;

    /* 8MB–12MB */
    for (i = 0; i < 1024; i++)
        page_table2[i] = (0x800000u + (uint32_t)(i * 0x1000)) | 3;

    /* 12MB–16MB */
    for (i = 0; i < 1024; i++)
        page_table3[i] = (0xC00000u + (uint32_t)(i * 0x1000)) | 3;

    /* Page directory: semua not-present dulu */
    for (i = 0; i < 1024; i++)
        page_directory[i] = 2;

    page_directory[0] = ((uint32_t)page_table)  | 7;  /* user-accessible */
    page_directory[1] = ((uint32_t)page_table1) | 3;
    page_directory[2] = ((uint32_t)page_table2) | 3;
    page_directory[3] = ((uint32_t)page_table3) | 3;

    load_page_directory(page_directory);
    enable_paging();
}

/* Baca nilai CR0 saat ini. */
uint32_t paging_get_cr0() {
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}

/*
 * Identity-map 8MB region di phys_addr untuk VBE LFB (1920×1080×32bpp = ~8MB).
 * Dua page tables dipasang di PD[pd_index] dan PD[pd_index+1], lalu TLB di-flush.
 */
void paging_map_vbe(uint32_t phys_addr) {
    uint32_t pd_index = phys_addr >> 22;
    int i;
    /* Block 0: phys_addr .. phys_addr + 4MB */
    for (i = 0; i < 1024; i++)
        vbe_page_table[i] = (phys_addr + (uint32_t)(i * 0x1000)) | 3;
    page_directory[pd_index] = ((uint32_t)vbe_page_table) | 3;

    /* Block 1: phys_addr+4MB .. phys_addr+8MB */
    uint32_t phys_next = phys_addr + 0x400000u;
    for (i = 0; i < 1024; i++)
        vbe_page_table2[i] = (phys_next + (uint32_t)(i * 0x1000)) | 3;
    page_directory[pd_index + 1] = ((uint32_t)vbe_page_table2) | 3;

    /* Flush TLB: reload CR3 */
    __asm__ volatile (
        "mov %%cr3, %%eax\n"
        "mov %%eax, %%cr3\n"
        ::: "eax"
    );
}