#include "lib.h"

// piper.c — Demo Phase 22: Pipe (satu proses)
// Membuka pipe, menulis beberapa pesan, lalu membacanya kembali.
// Menunjukkan cara kerja ring buffer pipe secara mandiri.

void _start() {
    print("[piper] membuka pipe...\n");
    int fd = pipe_open();
    if (fd < 0) {
        print("[piper] ERROR: gagal alokasi pipe!\n");
        exit();
    }

    char buf[8];
    itoa(fd, buf);
    print("[piper] pipe terbuka (id=");
    print(buf);
    print(")\n");

    // Tulis 3 pesan ke pipe
    pipe_write(fd, "pesan pertama");
    print("[piper] tulis: pesan pertama\n");

    pipe_write(fd, "pesan kedua");
    print("[piper] tulis: pesan kedua\n");

    pipe_write(fd, "pesan ketiga");
    print("[piper] tulis: pesan ketiga\n");

    print("[piper] --- membaca kembali ---\n");

    // Baca kembali semua pesan
    char rbuf[64];
    int i;
    for (i = 0; i < 3; i++) {
        pipe_read(fd, rbuf);
        print("[piper] baca : ");
        print(rbuf);
        print("\n");
    }

    pipe_close(fd);
    print("[piper] pipe ditutup, selesai!\n");
    exit();
}
