/* shm.c — Shared Memory antar proses (E4) */
#include "shm.h"
#include "vmm.h"

static ShmSlot shm_slots[SHM_MAX];

static int shm_streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == '\0' && b[i] == '\0';
}
static void shm_strcpy(char *dst, const char *src, int n) {
    int i = 0;
    while (src[i] && i < n - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void shm_init(void) {
    int i;
    for (i = 0; i < SHM_MAX; i++) {
        shm_slots[i].phys     = 0;
        shm_slots[i].refcount = 0;
        shm_slots[i].key[0]   = '\0';
    }
}

int shm_create(const char *key) {
    int i;
    /* Cek duplikat key */
    for (i = 0; i < SHM_MAX; i++)
        if (shm_slots[i].phys && shm_streq(shm_slots[i].key, key)) return i;
    /* Cari slot kosong */
    for (i = 0; i < SHM_MAX; i++) {
        if (!shm_slots[i].phys) {
            uint64_t frame = pmm_alloc_frame();
            if (!frame) return -1;
            /* Zero-fill frame */
            uint8_t *p = (uint8_t *)frame;
            int j;
            for (j = 0; j < SHM_PAGE_SIZE; j++) p[j] = 0;
            shm_slots[i].phys     = frame;
            shm_slots[i].refcount = 0;
            shm_strcpy(shm_slots[i].key, key, 16);
            return i;
        }
    }
    return -1;
}

int shm_find(const char *key) {
    int i;
    for (i = 0; i < SHM_MAX; i++)
        if (shm_slots[i].phys && shm_streq(shm_slots[i].key, key)) return i;
    return -1;
}

uint64_t shm_phys(int id) {
    if (id < 0 || id >= SHM_MAX) return 0;
    return shm_slots[id].phys;
}

/* Map region ke pml4 proses pada VA = SHM_VA_BASE + id * SHM_PAGE_SIZE.
 * Return virtual address, atau 0 jika gagal. */
int shm_attach(int id, uint64_t *pml4) {
    if (id < 0 || id >= SHM_MAX || !shm_slots[id].phys) return 0;
    uint64_t va = SHM_VA_BASE + (uint64_t)id * SHM_PAGE_SIZE;
    vmm_map_page(pml4, va, shm_slots[id].phys, 7);
    shm_slots[id].refcount++;
    return (int)va;
}

int shm_detach(int id, uint64_t *pml4) {
    if (id < 0 || id >= SHM_MAX || !shm_slots[id].phys) return 0;
    if (shm_slots[id].refcount > 0) shm_slots[id].refcount--;
    /* Tidak unmap halaman dari page table (tidak ada vmm_unmap yet) */
    return 1;
}

void shm_destroy(int id) {
    if (id < 0 || id >= SHM_MAX || !shm_slots[id].phys) return;
    /* Hanya bebaskan jika tidak ada yang attach */
    if (shm_slots[id].refcount <= 0) {
        pmm_free_frame(shm_slots[id].phys);
        shm_slots[id].phys     = 0;
        shm_slots[id].refcount = 0;
        shm_slots[id].key[0]   = '\0';
    }
}
