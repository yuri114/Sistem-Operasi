#include "semaphore.h"
#include "task.h"

static Semaphore sems[SEM_MAX];

void sem_init_all() {
    int i, j;
    for (i = 0; i < SEM_MAX; i++) {
        sems[i].used  = 0;
        sems[i].value = 0;
        sems[i].whead = 0;
        sems[i].wtail = 0;
        for (j = 0; j < SEM_WAITER_MAX; j++) sems[i].waiters[j] = -1;
    }
}

int sem_alloc(int initial_value) {
    int i, j;
    for (i = 0; i < SEM_MAX; i++) {
        if (!sems[i].used) {
            sems[i].used  = 1;
            sems[i].value = (int8_t)initial_value;
            sems[i].whead = 0;
            sems[i].wtail = 0;
            for (j = 0; j < SEM_WAITER_MAX; j++) sems[i].waiters[j] = -1;
            return i;
        }
    }
    return -1;
}

void sem_free(int id) {
    if (id >= 0 && id < SEM_MAX)
        sems[id].used = 0;
}

/* Enqueue tid into waiter ring. Returns 0 on success, -1 if full. */
static int sem_enqueue(int id, int tid) {
    uint8_t next = (sems[id].wtail + 1) % SEM_WAITER_MAX;
    if (next == sems[id].whead) return -1;  /* ring full */
    sems[id].waiters[sems[id].wtail] = tid;
    sems[id].wtail = next;
    return 0;
}

/* Dequeue one waiter. Returns tid or -1 if empty. */
static int sem_dequeue(int id) {
    if (sems[id].whead == sems[id].wtail) return -1;  /* ring empty */
    int tid = sems[id].waiters[sems[id].whead];
    sems[id].waiters[sems[id].whead] = -1;
    sems[id].whead = (sems[id].whead + 1) % SEM_WAITER_MAX;
    return tid;
}

/* sem_wait: decrement semaphore.
 * Jika value == 0, enqueue current task dan task_block (tidur sejati). */
int sem_wait(int id) {
    if (id < 0 || id >= SEM_MAX || !sems[id].used) return -1;
    while (1) {
        __asm__ volatile ("cli");
        if (sems[id].value > 0) {
            sems[id].value--;
            __asm__ volatile ("sti");
            return 0;
        }
        /* Tidak tersedia — enqueue diri ke ring waiter, lalu blokir */
        sem_enqueue(id, task_get_current());
        __asm__ volatile ("sti");
        task_block();  /* tidur sampai sem_post membangunkan */
        /* Setelah dibangunkan, coba cek ulang (spurious wake safe) */
    }
}

/* sem_post: increment semaphore dan bangunkan satu waiter. */
int sem_post(int id) {
    int w;
    if (id < 0 || id >= SEM_MAX || !sems[id].used) return -1;
    __asm__ volatile ("cli");
    w = sem_dequeue(id);
    sems[id].value++;
    __asm__ volatile ("sti");
    if (w >= 0 && w < MAX_TASKS && task_is_used(w))
        task_unblock(w);
    return 0;
}
