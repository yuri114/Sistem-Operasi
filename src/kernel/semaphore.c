#include "semaphore.h"

static Semaphore sems[SEM_MAX];

void sem_init_all() {
    int i;
    for (i = 0; i < SEM_MAX; i++) {
        sems[i].used  = 0;
        sems[i].value = 0;
    }
}

// Alokasi semaphore baru dengan nilai awal initial_value
// return ID (0..SEM_MAX-1) atau -1 jika penuh
int sem_alloc(int initial_value) {
    int i;
    for (i = 0; i < SEM_MAX; i++) {
        if (!sems[i].used) {
            sems[i].used  = 1;
            sems[i].value = (int8_t)initial_value;
            return i;
        }
    }
    return -1;
}

void sem_free(int id) {
    if (id >= 0 && id < SEM_MAX)
        sems[id].used = 0;
}

// sem_wait: kurangi nilai semaphore.
// Jika nilai sudah 0 (kritis sedang dipakai), busy-wait sampai bebas.
// Menggunakan disable interrupt (cli/sti) agar operasi atomik.
int sem_wait(int id) {
    if (id < 0 || id >= SEM_MAX || !sems[id].used) return -1;
    // Busy-wait: tunggu sampai value > 0
    while (1) {
        __asm__ volatile ("cli");
        if (sems[id].value > 0) {
            sems[id].value--;
            __asm__ volatile ("sti");
            return 0;
        }
        __asm__ volatile ("sti");
        // beri kesempatan task lain berjalan
        __asm__ volatile ("hlt");
    }
}

// sem_post: tambah nilai semaphore (release).
int sem_post(int id) {
    if (id < 0 || id >= SEM_MAX || !sems[id].used) return -1;
    __asm__ volatile ("cli");
    sems[id].value++;
    __asm__ volatile ("sti");
    return 0;
}
