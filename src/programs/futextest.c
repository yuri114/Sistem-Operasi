#include "lib.h"

/* futextest.c — Fondasi U: demo Futex + TLS
 *
 * 4 thread masing-masing menginkremen shared counter 1000×
 * menggunakan mutex berbasis futex.  Hasil akhir harus = 4000.
 *
 * Setiap thread juga membaca alamat TLS-nya via SYS_GET_TLS untuk
 * membuktikan bahwa kernel telah mengalokasikan halaman TLS unik. */

#define N_THREADS  4
#define N_ITER     1000

static Mutex    counter_lock = { 0 };
static volatile int counter  = 0;

/* Thread worker: kunci mutex, naik counter, buka mutex, ulangi. */
void worker(long arg) {
    char buf[24];
    int tid_self = getpid();

    /* Cetak alamat TLS thread ini (VA dalam range 32-bit cukup untuk itoa) */
    void *tls = get_tls();
    print("[futextest] thread tid=");
    itoa((unsigned int)tid_self, buf); print(buf);
    print(" TLS_VA=");
    itoa((unsigned int)(unsigned long)tls, buf); print(buf);
    print("\n");

    int i;
    for (i = 0; i < N_ITER; i++) {
        mutex_lock(&counter_lock);
        counter++;
        mutex_unlock(&counter_lock);
    }

    thread_exit();
}

void _start() {
    char buf[24];
    int tids[N_THREADS];
    int i;

    print("[futextest] mulai: 4 thread x 1000 iterasi\n");

    for (i = 0; i < N_THREADS; i++) {
        tids[i] = thread_create(worker, (void*)(long)i);
        if (tids[i] < 0) {
            print("[futextest] GAGAL membuat thread!\n");
            exit_code(1);
        }
    }

    /* Tunggu semua thread selesai */
    for (i = 0; i < N_THREADS; i++)
        thread_join(tids[i]);

    print("[futextest] counter = ");
    itoa((unsigned int)counter, buf); print(buf);
    print(" (harus 4000)\n");

    if (counter == N_THREADS * N_ITER)
        print("[futextest] LULUS: mutex futex bekerja dengan benar!\n");
    else
        print("[futextest] GAGAL: race condition terdeteksi!\n");

    exit_code(counter == N_THREADS * N_ITER ? 0 : 1);
}
