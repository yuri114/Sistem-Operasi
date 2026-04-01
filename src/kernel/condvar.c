/* condvar.c — Condition Variable implementation (F-P4) */
#include "condvar.h"
#include "semaphore.h"
#include "task.h"

static CondVar cvars[CV_MAX];

/* ---- internal ring helpers ----------------------------------------- */
static int cv_enqueue(CondVar *cv, int tid) {
    uint8_t next = (cv->wtail + 1) % CV_WAITER_MAX;
    if (next == cv->whead) return -1;  /* penuh */
    cv->waiters[cv->wtail] = tid;
    cv->wtail = next;
    return 0;
}

static int cv_dequeue(CondVar *cv) {
    if (cv->whead == cv->wtail) return -1;  /* kosong */
    int tid = cv->waiters[cv->whead];
    cv->whead = (cv->whead + 1) % CV_WAITER_MAX;
    return tid;
}

/* ------------------------------------------------------------------ */
void cv_init_all() {
    int i, j;
    for (i = 0; i < CV_MAX; i++) {
        cvars[i].used  = 0;
        cvars[i].whead = 0;
        cvars[i].wtail = 0;
        for (j = 0; j < CV_WAITER_MAX; j++) cvars[i].waiters[j] = -1;
    }
}

int cv_alloc() {
    int i;
    for (i = 0; i < CV_MAX; i++) {
        if (!cvars[i].used) {
            cvars[i].used  = 1;
            cvars[i].whead = 0;
            cvars[i].wtail = 0;
            return i;
        }
    }
    return -1;
}

void cv_free(int id) {
    if (id >= 0 && id < CV_MAX) cvars[id].used = 0;
}

/* cv_wait:
 *   1. Masukkan diri ke antrian waiter condvar (dengan IRQ off).
 *   2. Post semaphore (lepaskan mutex) secara atomik sebelum tidur.
 *   3. Tidur (task_block).
 *   4. Setelah dibangunkan: reacquire semaphore. */
int cv_wait(int id, int sem_id) {
    if (id < 0 || id >= CV_MAX || !cvars[id].used) return -1;
    __asm__ volatile ("cli");
    cv_enqueue(&cvars[id], task_get_current());
    sem_post(sem_id);               /* lepas mutex (IRQ cepat diaktifkan kembali di dalam) */
    __asm__ volatile ("sti");
    task_block();                   /* tidur */
    sem_wait(sem_id);               /* reacquire mutex setelah dibangunkan */
    return 0;
}

/* cv_signal: bangunkan 1 waiter FIFO */
int cv_signal(int id) {
    if (id < 0 || id >= CV_MAX || !cvars[id].used) return -1;
    __asm__ volatile ("cli");
    int w = cv_dequeue(&cvars[id]);
    __asm__ volatile ("sti");
    if (w >= 0) task_unblock(w);
    return 0;
}

/* cv_broadcast: bangunkan semua waiter */
int cv_broadcast(int id) {
    if (id < 0 || id >= CV_MAX || !cvars[id].used) return -1;
    __asm__ volatile ("cli");
    int w;
    while ((w = cv_dequeue(&cvars[id])) >= 0) {
        __asm__ volatile ("sti");
        task_unblock(w);
        __asm__ volatile ("cli");
    }
    __asm__ volatile ("sti");
    return 0;
}
