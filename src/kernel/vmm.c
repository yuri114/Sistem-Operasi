/* vmm.c ? Physical and Virtual Memory Manager (64-bit, 4-level paging)
 *
 * PMM: bitmap untuk 16MB / 4KB = 4096 frame.
 *   Frame 0-767  (0-3MB)   : kernel / boot page tables ? ditandai USED.
 *   Frame 768-4095 (3-16MB): bebas, dialokasikan untuk user processes.
 *
 * VMM per-proses:
 *   Setiap proses mempunyai PML4 sendiri.
 *   PML4[0] -> proc_pdpt -> proc_pd_low  (0-1GB, per-proses)
 *   proc_pdpt[1] = 0x4003  (boot PD 1-2GB, shared, identity-mapped)
 *   proc_pdpt[2] = 0x5003  (boot PD 2-3GB, shared)
 *   proc_pdpt[3] = 0x6003  (boot PD 3-4GB, shared, VBE LFB ada di sini)
 *   proc_pd_low[0] = 2MB large page 0x000000 (kernel code, P+RW+User+PS)
 *   proc_pd_low[1..N] = PT 4KB pages untuk user code/stack (isi vmm_map_page)
 */
#include "vmm.h"
#include "memory.h"

/* ===================================================================
 * Physical Memory Manager
 * =================================================================== */
#define TOTAL_FRAMES 4096   /* 16MB / 4KB */
static uint8_t frame_bitmap[TOTAL_FRAMES / 8];

static void bitmap_set(uint32_t frame) {
    frame_bitmap[frame / 8] |= (1 << (frame % 8));
}
static void bitmap_clear(uint32_t frame) {
    frame_bitmap[frame / 8] &= (uint8_t)~(1 << (frame % 8));
}
static int bitmap_test(uint32_t frame) {
    return frame_bitmap[frame / 8] & (1 << (frame % 8));
}

void pmm_init() {
    int i;
    for (i = 0; i < TOTAL_FRAMES / 8; i++) frame_bitmap[i] = 0;
    /* Tandai 0-3MB (768 frame) sebagai sudah dipakai */
    for (i = 0; i < 768; i++) bitmap_set(i);
}

uint64_t pmm_alloc_frame() {
    int i;
    for (i = 768; i < TOTAL_FRAMES; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            return (uint64_t)i * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_frame(uint64_t addr) {
    uint32_t frame = (uint32_t)(addr / PAGE_SIZE);
    if (frame >= 768 && frame < TOTAL_FRAMES)
        bitmap_clear(frame);
}

/* ===================================================================
 * Helpers
 * =================================================================== */
static void zero_frame(uint64_t phys) {
    uint64_t *p = (uint64_t *)phys;
    int i;
    for (i = 0; i < (int)(PAGE_SIZE / 8); i++) p[i] = 0;
}

/* ===================================================================
 * Virtual Memory Manager (4-level paging)
 * =================================================================== */

void vmm_switch_dir(uint64_t *pml4) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"((uint64_t)pml4) : "memory");
}

/*
 * Petakan satu halaman 4KB: virtual virt -> physical phys.
 * Lewati jika PDE adalah 2MB large page (bit PS).
 */
void vmm_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    uint64_t mask     = ~(uint64_t)0xFFF;

    /* PML4 -> PDPT */
    uint64_t *pdpt;
    if (pml4[pml4_idx] & 1) {
        pdpt = (uint64_t *)(pml4[pml4_idx] & mask);
    } else {
        uint64_t p = pmm_alloc_frame();
        if (!p) return;
        zero_frame(p);
        pdpt = (uint64_t *)p;
        pml4[pml4_idx] = p | flags | 1;
    }

    /* PDPT -> PD */
    uint64_t *pd;
    if (pdpt[pdpt_idx] & 1) {
        pd = (uint64_t *)(pdpt[pdpt_idx] & mask);
    } else {
        uint64_t p = pmm_alloc_frame();
        if (!p) return;
        zero_frame(p);
        pd = (uint64_t *)p;
        pdpt[pdpt_idx] = p | flags | 1;
    }

    /* Cek apakah PDE adalah 2MB large page (bit 7 = PS) */
    if (pd[pd_idx] & (1ULL << 7)) return;

    /* PD -> PT */
    uint64_t *pt;
    if (pd[pd_idx] & 1) {
        pt = (uint64_t *)(pd[pd_idx] & mask);
    } else {
        uint64_t p = pmm_alloc_frame();
        if (!p) return;
        zero_frame(p);
        pt = (uint64_t *)p;
        pd[pd_idx] = p | flags | 1;
    }

    pt[pt_idx] = (phys & mask) | flags | 1;
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

/*
 * Buat PML4 baru untuk proses user:
 *   pml4[0] -> proc_pdpt
 *   proc_pdpt[0] -> proc_pd_low  (0-1GB, per-proses)
 *   proc_pdpt[1] = 0x4003        (boot PD 1-2GB)
 *   proc_pdpt[2] = 0x5003        (boot PD 2-3GB)
 *   proc_pdpt[3] = 0x6003        (boot PD 3-4GB, VBE LFB)
 *   proc_pd_low[0] = 2MB large page 0x000000|0x87 (P+RW+User+PS)
 */
uint64_t *vmm_create_page_dir() {
    uint64_t pml4_phys = pmm_alloc_frame();
    if (!pml4_phys) return 0;
    zero_frame(pml4_phys);
    uint64_t *pml4 = (uint64_t *)pml4_phys;

    uint64_t pdpt_phys = pmm_alloc_frame();
    if (!pdpt_phys) { pmm_free_frame(pml4_phys); return 0; }
    zero_frame(pdpt_phys);
    uint64_t *pdpt = (uint64_t *)pdpt_phys;

    uint64_t pd_low_phys = pmm_alloc_frame();
    if (!pd_low_phys) { pmm_free_frame(pdpt_phys); pmm_free_frame(pml4_phys); return 0; }
    zero_frame(pd_low_phys);
    uint64_t *pd_low = (uint64_t *)pd_low_phys;

    /* pml4[0] -> proc_pdpt (P+RW+User) */
    pml4[0] = pdpt_phys | 7;

    /* proc_pdpt: [0]=pd_low (User), [1-3]=boot PDs (kernel-only) */
    pdpt[0] = pd_low_phys | 7;
    pdpt[1] = 0x4003ULL;   /* boot PD 1-2GB */
    pdpt[2] = 0x5003ULL;   /* boot PD 2-3GB */
    pdpt[3] = 0x6003ULL;   /* boot PD 3-4GB (VBE LFB) */

    /* pd_low[0] = 2MB large page: identity map 0-2MB, User-accessible */
    pd_low[0] = 0x0000000000000087ULL;   /* base=0, P+RW+User+PS */
    /* pd_low[1] = 2MB large page: identity map 2-4MB, User-accessible.
     * Diperlukan agar kernel heap (0x100000-0x2FFFFF) tetap accessible
     * saat CR3 user process aktif.  Juga mencakup range 0x300000-0x3FFFFF. */
    pd_low[1] = 0x0000000000200087ULL;   /* base=0x200000, P+RW+User+PS */

    return pml4;
}

/*
 * Bebaskan semua frame user: PTs, data frames (>= 3MB), pd_low, pdpt, pml4.
 * Tidak menyentuh boot PDs di 0x4000/0x5000/0x6000.
 */
void vmm_free_user_memory(uint64_t *pml4) {
    if (!pml4) return;
    uint64_t mask = ~(uint64_t)0xFFF;
    int i, j, k;

    /* Hanya iterasi pml4[0] (0-1GB, per-proses).
     * pml4[1..511] tidak diset untuk user processes. */
    if (!(pml4[0] & 1)) goto free_pml4;
    uint64_t *pdpt = (uint64_t *)(pml4[0] & mask);

    /* proc_pdpt[0] = proc_pd_low (per-proses).
     * proc_pdpt[1..3] = boot PDs (shared, jangan dibebaskan). */
    if (!(pdpt[0] & 1)) goto free_pdpt;
    uint64_t *pd_low = (uint64_t *)(pdpt[0] & mask);

    for (i = 0; i < 512; i++) {
        if (!(pd_low[i] & 1)) continue;
        /* pd_low[0] = 2MB large page (bit PS), lewati */
        if (pd_low[i] & (1ULL << 7)) continue;

        /* Ini PT: bebaskan setiap 4KB frame di dalamnya (jika >= 3MB) */
        uint64_t *pt = (uint64_t *)(pd_low[i] & mask);
        for (j = 0; j < 512; j++) {
            if (!(pt[j] & 1)) continue;
            uint64_t phys = pt[j] & mask;
            if (phys >= 768ULL * PAGE_SIZE)
                pmm_free_frame(phys);
        }
        /* Bebaskan frame PT itu sendiri */
        uint64_t pt_phys = pd_low[i] & mask;
        if (pt_phys >= 768ULL * PAGE_SIZE)
            pmm_free_frame(pt_phys);
    }

    /* Bebaskan proc_pd_low */
    uint64_t pd_low_phys = pdpt[0] & mask;
    if (pd_low_phys >= 768ULL * PAGE_SIZE)
        pmm_free_frame(pd_low_phys);

free_pdpt:
    /* Bebaskan proc_pdpt */
    uint64_t pdpt_phys = pml4[0] & mask;
    if (pdpt_phys >= 768ULL * PAGE_SIZE)
        pmm_free_frame(pdpt_phys);

free_pml4:
    /* Bebaskan pml4 itu sendiri */
    uint64_t pml4_phys = (uint64_t)pml4;
    if (pml4_phys >= 768ULL * PAGE_SIZE)
        pmm_free_frame(pml4_phys);

    (void)k;
}