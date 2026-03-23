#include "vmm.h"
#include "memory.h"

// 16MB / 4KB = 4096 frame, butuh 4096/8 = 512 byte untuk bitmap
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
    // Tandai 3MB pertama (768 frame) sebagai sudah dipakai:
    // frame 0-767 = kernel binary, heap, page tables statik
    for (i=0; i < 768 ; i++) {
        bitmap_set(i);
    }
}

uint32_t pmm_alloc_frame() {
    int i;
    // scan frame 768-4095 (3MB–16MB), semuanya identity-mapped
    for(i = 768; i < TOTAL_FRAMES; i++) {
        if (!bitmap_test(i)) { //cari frame yang bebas
            bitmap_set(i); //tandai frame ini sebagai dipakai
            return (uint32_t)((uint32_t)i * PAGE_SIZE); //kembalikan alamat fisik frame ini
        }
    }
    return 0; // tidak ada frame yang tersedia
}

void pmm_free_frame(uint32_t addr) {
    uint32_t frame = addr / PAGE_SIZE;
    if (frame >= 768 && frame < TOTAL_FRAMES)
        bitmap_clear(frame); //bebaskan frame dengan clear bit di bitmap
}

// Zero-fill satu frame berdasarkan alamat fisiknya
static void zero_frame(uint32_t phys) {
    uint8_t *p = (uint8_t*)phys;
    uint32_t i;
    for (i = 0; i < PAGE_SIZE; i++) p[i] = 0;
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
    else { //jika belum ada, buat baru dari PMM (4KB-aligned, bisa di-free)
        uint32_t pt_phys = pmm_alloc_frame();
        if (!pt_phys) return;  // kehabisan frame
        zero_frame(pt_phys);
        page_table = (uint32_t*)pt_phys;
        page_dir[pd_idx] = pt_phys | flags | 1; //set entry di page directory dengan alamat page table + flag present
    }
    page_table[pt_idx] = (phys & 0xFFFFF000) | flags | 1; //set entry di page table dengan alamat fisik + flag present
    /* Flush TLB untuk alamat virtual ini agar tidak pakai entry lama */
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

uint32_t* vmm_create_page_dir() {
    // Alokasi page directory baru dari PMM (4KB-aligned)
    uint32_t dir_phys = pmm_alloc_frame();
    if (!dir_phys) return 0;
    zero_frame(dir_phys);
    uint32_t *dir = (uint32_t*)dir_phys;

    extern uint32_t page_directory[]; //gunakan page directory kernel yang sudah ada untuk mapping 4MB pertama

    // deep-copy page table kernel (bukan copy pointer) agar tiap proses punya page table sendiri
    // sehingga vmm_map_page tidak merusak mapping kernel
    if (page_directory[0] & 1) {
        uint32_t *kernel_pt = (uint32_t*)(page_directory[0] & 0xFFFFF000);
        uint32_t new_pt_phys = pmm_alloc_frame();
        if (!new_pt_phys) { pmm_free_frame(dir_phys); return 0; }
        uint32_t *new_pt = (uint32_t*)new_pt_phys;
        int i;
        for (i = 0; i < 1024; i++) new_pt[i] = kernel_pt[i];
        dir[0] = new_pt_phys | (page_directory[0] & 0xFFF); //same flags, new page table
    }

    /* Salin mapping VBE LFB ke page directory proses baru.
     * Gunakan runtime address (gfx_lfb_addr) bukan hardcode 0xE0000000. */
    extern uint32_t gfx_lfb_addr;
    uint32_t lfb_pd_idx = gfx_lfb_addr >> 22;
    if (lfb_pd_idx > 0 && (page_directory[lfb_pd_idx] & 1)) {
        dir[lfb_pd_idx] = page_directory[lfb_pd_idx]; /* share VBE page table */
    }

    return dir;
}

/* Bebaskan semua frame yang milik proses user: page directory, page tables,
 * dan semua frame data user (fisik >= 3MB = frame >= 768).
 * Jangan panggil untuk page_directory kernel (tasks[0]).  */
void vmm_free_user_memory(uint32_t *page_dir) {
    if (!page_dir) return;

    extern uint32_t gfx_lfb_addr;
    uint32_t lfb_pd_idx = gfx_lfb_addr >> 22;

    int i, j;
    for (i = 0; i < 1024; i++) {
        if ((uint32_t)i == lfb_pd_idx) continue;  /* VBE shared — jangan disentuh */
        if (!(page_dir[i] & 1)) continue;          /* entry tidak present, skip */

        uint32_t *pt = (uint32_t*)(page_dir[i] & 0xFFFFF000);

        /* pd_idx=0: lewati 768 entry pertama (kernel 0–3MB, jangan dibebaskan) */
        int start_j = (i == 0) ? 768 : 0;

        for (j = start_j; j < 1024; j++) {
            if (pt[j] & 1) {
                uint32_t phys = pt[j] & 0xFFFFF000;
                /* Hanya bebaskan frame PMM (>= 3MB = frame 768+) */
                if (phys >= 768u * PAGE_SIZE)
                    pmm_free_frame(phys);
            }
        }

        /* Bebaskan frame page table itu sendiri jika dari PMM */
        if ((uint32_t)pt >= 768u * PAGE_SIZE)
            pmm_free_frame((uint32_t)pt);
    }

    /* Bebaskan frame page directory */
    if ((uint32_t)page_dir >= 768u * PAGE_SIZE)
        pmm_free_frame((uint32_t)page_dir);
}


