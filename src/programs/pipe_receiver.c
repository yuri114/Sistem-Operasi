#include "lib.h"

// pipe_receiver.c — Demo Phase 22: Multi-process Pipe (sisi penerima)
// Dijalankan via: pipe pipe_sender pipe_receiver
// Mendapat pipe_id dari shell lewat syscall SYS_PIPE_GETID (inherited pipe).

void _start() {
    // Ambil pipe yang di-assign oleh shell
    int fd = pipe_get_id();
    if (fd < 0) {
        print("[pipe_receiver] ERROR: tidak ada pipe!\n");
        exit();
    }

    char buf[8];
    itoa(fd, buf);
    print("[pipe_receiver] pipe id=");
    print(buf);
    print(" siap, menunggu pesan...\n");

    char rbuf[64];
    int count = 0;
    // Baca 3 pesan — spin jika pipe masih kosong (sender belum kirim)
    while (count < 3) {
        int r = pipe_read(fd, rbuf);
        if (r > 0) {
            print("[pipe_receiver] terima: ");
            print(rbuf);
            print("\n");
            count++;
        }
        // jika kosong (r==0), lanjut spin — scheduler akan jalankan sender
    }

    pipe_close(fd);
    print("[pipe_receiver] semua pesan diterima, selesai!\n");
    exit();
}
