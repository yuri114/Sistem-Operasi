#include "semaphore.h"
#include "task.h"

static Semaphore sems[SEM_MAX];

void sem_init_all() {
    int i;
    for (i = 0; i < SEM_MAX; i++) {
        sems[i].used   = 0;
        sems[i].value  = 0;
        sems[i].waiter = -1;
    }
}

// Alokasi semaphore baru dengan nilai awal initial_value
// return ID (0..SEM_MAX-1) atau -1 jika penuh
int sem_alloc(int initial_value) {
    int i;
    for (i = 0; i < SEM_MAX; i++) {
        if (!sems[i].used) {
            sems[i].used   = 1;
            sems[i].value  = (int8_t)initial_value;
            sems[i].waiter = -1;
            return i;
        }
    }
    return -1;
}

void sem_free(int id) {
    if (id >= 0 && id < SEM_MAX)
        sems[id].used = 0;
}

/* sem_wait: kurangi nilai semaphore.
 * Jika nilai == 0, blokir task ini (task_block) sampai sem_post membangunkan. */
int sem_wait(int id) {
    if (id < 0 || id >= SEM_MAX || !sems[id].used) return -1;
    while (1) {
        __asm__ volatile ("cli");
        if (sems[id].value > 0) {
            sems[id].value--;
            __asm__ volatile ("sti");
            return 0;
        }
        /* Tidak tersedia — simpan waiter lalu blokir */
        sems[id].waiter = task_get_current();
        __asm__ volatile ("sti");
        task_block();  /* tidur sampai sem_post memanggil task_unblock */
        /* Setelah dibangunkan, coba lagi (loop while) */
    }
}

/* sem_post: tambah nilai semaphore dan bangunkan waiter jika ada. */
int sem_post(int id) {
    if (id < 0 || id >= SEM_MAX || !sems[id].used) return -1;
    __asm__ volatile ("cli");
    int w = sems[id].waiter;
    sems[id].waiter = -1;
    sems[id].value++;
    __asm__ volatile ("sti");
    if (w >= 0 && w < MAX_TASKS && task_is_used(w))
        task_unblock(w);
    return 0;
}
