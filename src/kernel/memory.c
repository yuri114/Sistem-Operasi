#include "memory.h"
#define HEAP_START 0x100000 //alamat awal heap (1MB)
#define HEAP_SIZE  0x100000 //ukuran heap (1MB)

static uint8_t *heap_ptr; //pointer ke posisi saat ini di heap

void mem_init() {
    heap_ptr = (uint8_t*) HEAP_START; //inisialisasi pointer heap ke alamat awal
}
void* malloc(uint32_t size) {
    if (heap_ptr + size > (uint8_t*)(HEAP_START + HEAP_SIZE)) {
        return 0;
    }
    void* addr= heap_ptr; //simpan alamat yang akan dikembalikan
    heap_ptr += size; //geser pointer heap ke posisi berikutnya
    return addr; //kembalikan alamat yang dialokasikan
}
void free(void* ptr) {
    // bump allocator tidak bisa benar-benar membebaskan memori, jadi fungsi ini kosong
    // fungsi ini sengaja dikosongkan
    (void)ptr; //hindari warning unused parameter
}