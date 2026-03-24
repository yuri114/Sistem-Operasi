#include "memory.h"

#define HEAP_START 0x100000u  /* 1MB — awal heap kernel */
#define HEAP_SIZE  0x100000u  /* 1MB — ukuran heap kernel */

/* Header setiap blok heap */
typedef struct BlockHeader {
    uint32_t           size;   /* bytes data (tidak termasuk header) */
    uint8_t            free;   /* 1 = bebas, 0 = terpakai */
    uint8_t            _pad[3];
    struct BlockHeader *next;  /* blok berikutnya dalam linked list */
} BlockHeader;

#define HEADER_SIZE ((uint32_t)sizeof(BlockHeader))  /* 12 bytes */

static BlockHeader *heap_head = 0;

void mem_init() {
    heap_head = (BlockHeader*)HEAP_START;
    heap_head->size   = HEAP_SIZE - HEADER_SIZE;
    heap_head->free   = 1;
    heap_head->_pad[0] = heap_head->_pad[1] = heap_head->_pad[2] = 0;
    heap_head->next   = 0;
}

void* malloc(uint32_t size) {
    if (!size) return 0;
    size = (size + 3u) & ~3u;  /* bulatkan ke kelipatan 4 byte */

    BlockHeader *curr = heap_head;
    while (curr) {
        if (curr->free && curr->size >= size) {
            /* Pecah blok jika cukup ruang untuk blok baru sesudahnya */
            if (curr->size >= size + HEADER_SIZE + 4u) {
                BlockHeader *split = (BlockHeader*)((uint8_t*)curr + HEADER_SIZE + size);
                split->size  = curr->size - size - HEADER_SIZE;
                split->free  = 1;
                split->_pad[0] = split->_pad[1] = split->_pad[2] = 0;
                split->next  = curr->next;
                curr->size   = size;
                curr->next   = split;
            }
            curr->free = 0;
            return (void*)((uint8_t*)curr + HEADER_SIZE);
        }
        curr = curr->next;
    }
    return 0;  /* kehabisan heap */
}

void free(void *ptr) {
    if (!ptr) return;
    BlockHeader *block = (BlockHeader*)((uint8_t*)ptr - HEADER_SIZE);
    block->free = 1;
    /* Coalescing pass: telusuri dari heap_head dan gabung semua blok bebas
     * yang bersebelahan. Menangani kasus blok sebelumnya maupun sesudahnya. */
    BlockHeader *curr = heap_head;
    while (curr) {
        if (curr->free && curr->next && curr->next->free) {
            /* Gabung curr dengan blok berikutnya */
            curr->size += HEADER_SIZE + curr->next->size;
            curr->next  = curr->next->next;
            /* Cek ulang curr (mungkin ada lebih banyak blok bebas setelahnya) */
        } else {
            curr = curr->next;
        }
    }
}