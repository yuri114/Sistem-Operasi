/* condtest.c — Demo Condition Variable (bukti F-P4 bekerja)
 *
 * Pola producer-consumer:
 *   - producer: sleep 300ms, set shared_msg, lalu cond_signal
 *   - consumer: tunggu condvar sampai buf_ready, cetak pesan
 *
 * Membuktikan bahwa cond_wait/cond_signal bekerja dengan semaphore mutex.
 */
#include "lib.h"

static volatile int buf_ready = 0;
static char shared_msg[64];
static int g_mtx, g_cv;

void producer(void *arg) {
    (void)arg;
    sleep_ms(300);                          /* beri consumer waktu wait dulu */

    sem_wait(g_mtx);
    /* tulis pesan */
    const char *src = "Halo dari producer!";
    int i = 0;
    while (src[i] && i < 62) { shared_msg[i] = src[i]; i++; }
    shared_msg[i] = '\0';
    buf_ready = 1;
    cond_signal(g_cv);                      /* bangunkan consumer */
    sem_post(g_mtx);

    print("producer: selesai.\n");
    thread_exit();
}

void consumer(void *arg) {
    (void)arg;
    sem_wait(g_mtx);
    while (!buf_ready)
        cond_wait(g_cv, g_mtx);            /* lepas mutex, tunggu sinyal */
    print("consumer terima: ");
    print(shared_msg);
    print("\n");
    sem_post(g_mtx);

    print("consumer: selesai.\n");
    thread_exit();
}

void _start() {
    print("condtest: mulai demo condition variable...\n");

    g_mtx = sem_alloc(1);
    g_cv  = cond_alloc();

    if (g_mtx < 0 || g_cv < 0) {
        print("condtest: GAGAL alokasi sem/condvar\n");
        exit();
    }

    int tc = thread_create(consumer,  (void*)0);
    int tp = thread_create(producer,  (void*)0);

    if (tc < 0 || tp < 0) {
        print("condtest: GAGAL spawn thread\n");
        exit();
    }

    thread_join(tc);
    thread_join(tp);

    cond_free(g_cv);
    sem_free(g_mtx);

    print("condtest: selesai. cond_wait/cond_signal OK.\n");
    exit();
}
