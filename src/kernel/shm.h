#ifndef SHM_H
#define SHM_H
#include <stdint.h>

/* ============================================================
 * SHM — Shared Memory sederhana antar proses
 *   Masing-masing SHM region dialokasi dari PMM (fisik).
 *   Setiap proses yang attach mendapat region ini di-map
 *   di virtual address SHM_BASE + slot * SHM_PAGE_SIZE
 *   dalam page directory-nya sendiri.
 *
 *   Batas: SHM_MAX slot, masing-masing SHM_PAGE_SIZE byte.
 * ============================================================ */

#define SHM_MAX        8          /* jumlah region bersamaan */
#define SHM_PAGE_SIZE  4096       /* ukuran per region (1 frame, 4KB) */
#define SHM_VA_BASE    0x500000ULL /* base VA di user space per region */

typedef struct {
    char     key[16];   /* string identifier */
    uint64_t phys;      /* physical frame address (dari PMM); 0 = kosong */
    int      refcount;  /* berapa proses sudah attach */
} ShmSlot;

void     shm_init(void);
int      shm_create(const char *key);                       /* return slot id atau -1 */
int      shm_find(const char *key);                         /* return slot id atau -1 */
uint64_t shm_phys(int id);                                  /* physical address slot */
int      shm_attach(int id, uint64_t *pml4);                /* map ke pml4, return VA atau 0 */
int      shm_detach(int id, uint64_t *pml4);
void     shm_destroy(int id);

#endif
