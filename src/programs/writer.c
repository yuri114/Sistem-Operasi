#include "lib.h"

// writer.c - Demo Semaphore Phase 21
// Program ini mengalokasikan semaphore (mutex), lalu melakukan
// 4 kali "tulis ke queue" dengan benar-benar mengunci sebelum
// menulis dan melepas setelahnya — membuktikan tidak ada race condition.

void _start() {
    print("[writer] memulai demo semaphore...\n");

    // Alokasi semaphore dengan nilai awal 1 (mutex tersedia)
    int sem = sem_alloc(1);
    if (sem < 0) {
        print("[writer] ERROR: gagal alokasi semaphore!\n");
        exit();
    }

    print("[writer] semaphore dialokasikan (id=");
    char buf[8];
    itoa(sem, buf);
    print(buf);
    print(")\n");

    // Simulasi 3 kali menulis ke shared resource (IPC queue) secara aman
    int i;
    for (i = 1; i <= 3; i++) {
        // KUNCI — masuk critical section
        sem_wait(sem);

        print("[writer] ===[ masuk critical section ke-");
        itoa(i, buf);
        print(buf);
        print(" ]===\n");

        // Kerja di dalam critical section
        msg_send("pesan aman dari writer");

        print("[writer] pesan ke-");
        itoa(i, buf);
        print(buf);
        print(" dikirim dengan aman\n");

        // LEPAS — keluar critical section
        sem_post(sem);

        print("[writer] keluar critical section\n");
    }

    // Bebaskan semaphore
    sem_free(sem);
    print("[writer] semaphore dibebaskan, selesai!\n");

    exit();
}
