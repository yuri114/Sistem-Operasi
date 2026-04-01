/* threadtest.c — Demo Tahap N: User-space Threading
 *
 * Spawns 3 threads. Each thread prints its ID and argument,
 * sleeps briefly, then exits.  The main program joins all 3
 * and reports completion.
 */
#include "lib.h"

/* Shared counter — no atomic needed for demo, just shows shared memory. */
static volatile int done = 0;

/* Thread function — receives argument cast to an integer. */
void thread_worker(void *arg) {
    int id = (int)(long)arg;

    print("  thread ");
    print_int(id);
    print(": mulai\n");

    /* Simulate some work with a busy-loop delay. */
    volatile int i;
    for (i = 0; i < 500000; i++);

    print("  thread ");
    print_int(id);
    print(": selesai\n");

    done++;          /* shared variable, fine for demo */
    thread_exit();   /* WAJIB — jangan return biasa */
}

void _start() {
    print("threadtest: mulai spawn 3 thread...\n");

    int t1 = thread_create(thread_worker, (void*)1);
    int t2 = thread_create(thread_worker, (void*)2);
    int t3 = thread_create(thread_worker, (void*)3);

    if (t1 < 0 || t2 < 0 || t3 < 0) {
        print("threadtest: GAGAL spawn thread (slot habis?)\n");
        exit();
    }

    print("threadtest: menunggu semua thread selesai...\n");

    thread_join(t1);
    thread_join(t2);
    thread_join(t3);

    print("threadtest: semua thread done. count=");
    print_int(done);
    print("\n");

    exit();
}
