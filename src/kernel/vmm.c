/* vmm.c ? Physical and Virtual Memory Manager (64-bit, 4-level paging)
 *
 * PMM: bitmap untuk 64MB / 4KB = 16384 frame.
 *   Frame 0-2047     (0-8MB)  : kernel image, heap, boot page tables —
 *                                ditandai USED (lihat KERNEL_RESERVED_FRAMES).
 *   Frame 2048-16383 (8-64MB) : bebas, dialokasikan untuk user processes.
 *
 * VMM per-proses:
 *   Setiap proses mempunyai PML4 sendiri.
 *   PML4[0] -> proc_pdpt -> proc_pd_low  (0-1GB, per-proses)
 *   proc_pdpt[1] = 0x4003  (boot PD 1-2GB, shared, identity-mapped)
 *   proc_pdpt[2] = 0x5003  (boot PD 2-3GB, shared)
 *   proc_pdpt[3] = 0x6003  (boot PD 3-4GB, shared, VBE LFB ada di sini)
 *   proc_pd_low[0-3] = 2MB large pages 0x000000-0x7FFFFF (heap + kernel
 *                      image @ 0x700000, P+RW+User+PS)
 *   proc_pd_low[4..N] = PT 4KB pages untuk user code/stack (isi vmm_map_page)
 */
#include "vmm.h"
#include "memory.h"

/* ===================================================================
 * Physical Memory Manager
 * =================================================================== */
#define TOTAL_FRAMES 16384  /* 64MB / 4KB */
/* Frame 0..KERNEL_RESERVED_FRAMES-1 (0-8MB) : kernel image, heap, boot page
 * tables, .bss/stack — ditandai USED, tidak pernah dialokasikan ke user.
 * Mencakup heap [0x100000-0x700000) dan kernel image @ 0x700000 (linker.ld). */
#define KERNEL_RESERVED_FRAMES 2048  /* 8MB / 4KB */
static uint8_t frame_bitmap[TOTAL_FRAMES / 8];
static uint8_t frame_cow_cnt[TOTAL_FRAMES];  /* COW ref-count per frame */

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
    /* Tandai 0-8MB (KERNEL_RESERVED_FRAMES frame) sebagai sudah dipakai */
    for (i = 0; i < KERNEL_RESERVED_FRAMES; i++) bitmap_set(i);
}

uint64_t pmm_alloc_frame() {
    int i;
    for (i = KERNEL_RESERVED_FRAMES; i < TOTAL_FRAMES; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            return (uint64_t)i * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_frame(uint64_t addr) {
    uint32_t frame = (uint32_t)(addr / PAGE_SIZE);
    if (frame >= KERNEL_RESERVED_FRAMES && frame < TOTAL_FRAMES)
        bitmap_clear(frame);
}

uint32_t pmm_free_count() {
    uint32_t count = 0;
    int i;
    for (i = KERNEL_RESERVED_FRAMES; i < TOTAL_FRAMES; i++)
        if (!bitmap_test(i)) count++;
    return count;
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

    /* PML4 -> PDPT
     * Intermediate entries HARUS P+RW+User=7 agar CPU bisa walk ke level berikutnya.
     * Proteksi akses (RO/RW, COW) ditentukan sepenuhnya di leaf PTE. */
    uint64_t *pdpt;
    if (pml4[pml4_idx] & 1) {
        pdpt = (uint64_t *)(pml4[pml4_idx] & mask);
    } else {
        uint64_t p = pmm_alloc_frame();
        if (!p) return;
        zero_frame(p);
        pdpt = (uint64_t *)p;
        pml4[pml4_idx] = p | 7;   /* P+RW+User — intermediate selalu writable */
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
        pdpt[pdpt_idx] = p | 7;   /* P+RW+User — intermediate selalu writable */
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
        pd[pd_idx] = p | 7;       /* P+RW+User — intermediate selalu writable */
    }

    pt[pt_idx] = (phys & mask) | flags | 1;  /* leaf PTE: pakai flags caller */
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

/*
 * Hapus pemetaan satu halaman 4KB (set PTE = 0, flush TLB).
 * Tidak membebaskan physical frame — caller bertanggung jawab.
 */
void vmm_unmap_page(uint64_t *pml4, uint64_t virt) {
    uint64_t mask     = ~(uint64_t)0xFFF;
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!pml4 || !(pml4[pml4_idx] & 1)) return;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & mask);
    if (!(pdpt[pdpt_idx] & 1)) return;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & mask);
    if (!(pd[pd_idx] & 1)) return;
    if (pd[pd_idx] & (1ULL << 7)) return;  /* 2MB large page, lewati */
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & mask);
    pt[pt_idx] = 0;
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
    /* pd_low[2..3] = 2MB large pages: identity map 4-8MB, User-accessible.
     * Heap penuh [0x100000-0x6FFFFF) + kernel image @ 0x700000 (linker.ld)
     * harus tetap accessible saat CR3 user process aktif (interrupt/syscall
     * berjalan dengan kernel .text di 0x700000+ walau CR3=punya proses user). */
    pd_low[2] = 0x0000000000400087ULL;   /* base=0x400000, P+RW+User+PS */
    pd_low[3] = 0x0000000000600087ULL;   /* base=0x600000, P+RW+User+PS */
    /* pd_low[32] = 2MB large page: identity map 64-66MB, kernel-only.
     * WAJIB: BSS kernel berada di 0x4000000-0x41FFFFF. Tanpa entry ini,
     * setiap akses ke global kernel (tasks[], frame_bitmap, dll.) saat
     * CR3 user process aktif akan menyebabkan #PF → crash. */
    pd_low[32] = 0x0000000004000083ULL;  /* base=0x4000000, P+RW, kernel-only, PS */

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
            if (phys >= (uint64_t)KERNEL_RESERVED_FRAMES * PAGE_SIZE) {
                /* Respek COW refcount: hanya free jika tidak ada proses lain yang berbagi */
                uint32_t fn = (uint32_t)(phys / PAGE_SIZE);
                if (fn < TOTAL_FRAMES && frame_cow_cnt[fn] > 0) {
                    frame_cow_cnt[fn]--;
                    /* Jangan pmm_free — masih dipakai proses lain */
                } else {
                    pmm_free_frame(phys);
                }
            }
        }
        /* Bebaskan frame PT itu sendiri */
        uint64_t pt_phys = pd_low[i] & mask;
        if (pt_phys >= (uint64_t)KERNEL_RESERVED_FRAMES * PAGE_SIZE)
            pmm_free_frame(pt_phys);
    }

    /* Bebaskan proc_pd_low */
    uint64_t pd_low_phys = pdpt[0] & mask;
    if (pd_low_phys >= (uint64_t)KERNEL_RESERVED_FRAMES * PAGE_SIZE)
        pmm_free_frame(pd_low_phys);

free_pdpt:
    /* Bebaskan proc_pdpt */
    uint64_t pdpt_phys = pml4[0] & mask;
    if (pdpt_phys >= (uint64_t)KERNEL_RESERVED_FRAMES * PAGE_SIZE)
        pmm_free_frame(pdpt_phys);

free_pml4:
    /* Bebaskan pml4 itu sendiri */
    uint64_t pml4_phys = (uint64_t)pml4;
    if (pml4_phys >= (uint64_t)KERNEL_RESERVED_FRAMES * PAGE_SIZE)
        pmm_free_frame(pml4_phys);

    (void)k;
}

/* ===================================================================
 * F-Q: Copy-on-Write helpers
 * =================================================================== */

/* Kembalikan alamat fisik untuk VA pada pml4, atau 0 jika tidak terpetakan. */
uint64_t vmm_get_phys(uint64_t *pml4, uint64_t virt) {
    if (!pml4) return 0;
    uint64_t mask     = ~(uint64_t)0xFFF;
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & 1)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & mask);
    if (!(pdpt[pdpt_idx] & 1)) return 0;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & mask);
    if (!(pd[pd_idx] & 1)) return 0;
    if (pd[pd_idx] & (1ULL << 7)) {
        /* 2MB large page */
        return (pd[pd_idx] & ~(uint64_t)0x1FFFFF) + (virt & 0x1FFFFF);
    }
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & mask);
    if (!(pt[pt_idx] & 1)) return 0;
    return pt[pt_idx] & mask;
}

/* Kembalikan pointer ke PTE untuk VA, atau NULL jika tidak ada PT. */
uint64_t *vmm_get_pte(uint64_t *pml4, uint64_t virt) {
    if (!pml4) return 0;
    uint64_t mask     = ~(uint64_t)0xFFF;
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & 1)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_idx] & mask);
    if (!(pdpt[pdpt_idx] & 1)) return 0;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_idx] & mask);
    if (!(pd[pd_idx] & 1)) return 0;
    if (pd[pd_idx] & (1ULL << 7)) return 0;  /* large page, tidak ada PT */
    uint64_t *pt = (uint64_t *)(pd[pd_idx] & mask);
    return &pt[pt_idx];
}

/*
 * vmm_copy_cow — setup Copy-on-Write antara parent dan child (fork).
 *
 * Untuk setiap halaman user (phys >= 3MB) di parent:
 *   - Set bit COW (bit 9) + clear RW pada PTE parent
 *   - Petakan halaman yang sama di child dengan flag yang sama (RO + COW)
 *   - Increment frame_cow_cnt
 *
 * Setelah ini, write pertama ke halaman bersama akan menyebabkan #PF
 * yang ditangani oleh vmm_cow_fault().
 */
void vmm_copy_cow(uint64_t *parent_pml4, uint64_t *child_pml4) {
    if (!parent_pml4 || !child_pml4) return;
    uint64_t mask = ~(uint64_t)0xFFF;
    int i, j;

    /* Hanya PML4[0] (0-1GB, user space per-proses) */
    if (!(parent_pml4[0] & 1)) return;
    uint64_t *parent_pdpt = (uint64_t *)(parent_pml4[0] & mask);

    /* Hanya PDPT[0] (proc_pd_low) */
    if (!(parent_pdpt[0] & 1)) return;
    uint64_t *parent_pd = (uint64_t *)(parent_pdpt[0] & mask);

    for (i = 0; i < 512; i++) {
        if (!(parent_pd[i] & 1)) continue;
        if (parent_pd[i] & (1ULL << 7)) continue;  /* 2MB large page = kernel, skip */

        uint64_t *parent_pt = (uint64_t *)(parent_pd[i] & mask);
        for (j = 0; j < 512; j++) {
            if (!(parent_pt[j] & 1)) continue;
            uint64_t phys = parent_pt[j] & mask;
            if (phys < (uint64_t)KERNEL_RESERVED_FRAMES * PAGE_SIZE) continue;  /* frame kernel, skip */

            /* VA halaman ini */
            uint64_t va = (uint64_t)i * 0x200000ULL + (uint64_t)j * 0x1000ULL;

            /* Tandai parent PTE: RO + COW (clear bit1, set bit9) */
            parent_pt[j] = phys | 0x205ULL;  /* P=1, RO, User=1, COW=bit9 */
            __asm__ volatile ("invlpg (%0)" :: "r"(va) : "memory");

            /* Petakan di child dengan flag yang sama */
            vmm_map_page(child_pml4, va, phys, 0x205ULL);

            /* Tambah ref count: parent + child masing-masing 1 counter = +2 */
            uint32_t fn = (uint32_t)(phys / PAGE_SIZE);
            if (fn < TOTAL_FRAMES) {
                if (frame_cow_cnt[fn] < 254) frame_cow_cnt[fn] += 2;
                else frame_cow_cnt[fn] = 255;
            }
        }
    }
}

/*
 * vmm_cow_fault — tangani write fault ke halaman COW.
 *
 * Dipanggil dari exception_handler ketika:
 *   error_code & 1  (present), error_code & 2  (write), error_code & 4  (user).
 *
 * Kembalikan 1 jika berhasil ditangani (instruksi boleh di-retry),
 * 0 jika bukan COW page (panic elsewhere).
 */
int vmm_cow_fault(uint64_t *pml4, uint64_t virt) {
    uint64_t fa   = virt & ~(uint64_t)0xFFF;
    uint64_t *pte = vmm_get_pte(pml4, fa);
    if (!pte || !(*pte & 1)) return 0;        /* halaman tidak present */
    if (!(*pte & PTE_COW))   return 0;        /* bukan COW */

    uint64_t old_phys  = *pte & ~(uint64_t)0xFFF;
    uint32_t old_frame = (uint32_t)(old_phys / PAGE_SIZE);

    if (old_frame < TOTAL_FRAMES && frame_cow_cnt[old_frame] > 1) {
        /* Masih ada proses lain yang berbagi frame ini — alokasi frame baru */
        uint64_t new_phys = pmm_alloc_frame();
        if (!new_phys) return 0;  /* OOM — biarkan kernel panic */
        /* Salin isi lama ke frame baru */
        uint8_t *src = (uint8_t *)old_phys;
        uint8_t *dst = (uint8_t *)new_phys;
        int k;
        for (k = 0; k < (int)PAGE_SIZE; k++) dst[k] = src[k];
        /* Remap dengan RW, clear COW */
        *pte = new_phys | 7ULL;  /* P+RW+User */
        frame_cow_cnt[old_frame]--;
    } else {
        /* Kita satu-satunya pemilik — cukup remap RW, clear COW */
        *pte = old_phys | 7ULL;  /* P+RW+User */
    }
    __asm__ volatile ("invlpg (%0)" :: "r"(fa) : "memory");
    return 1;
}