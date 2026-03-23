#include "lib.h"

// pipe_sender.c — Demo Phase 22: Multi-process Pipe (sisi pengirim)
// Dijalankan via: pipe pipe_sender pipe_receiver
// Mendapat pipe_id dari shell lewat syscall SYS_PIPE_GETID (inherited pipe).

void _start() {
    // Ambil pipe yang di-assign oleh shell
    int fd = pipe_get_id();
    if (fd < 0) {
        print("[pipe_sender] ERROR: tidak ada pipe! Gunakan: pipe pipe_sender pipe_receiver\n");
        exit();
    }

    char buf[8];
    itoa(fd, buf);
    print("[pipe_sender] pipe id=");
    print(buf);
    print(" siap, mulai kirim...\n");

    pipe_write(fd, "halo dari sender — pesan 1");
    print("[pipe_sender] kirim pesan 1\n");

    pipe_write(fd, "halo dari sender — pesan 2");
    print("[pipe_sender] kirim pesan 2\n");

    pipe_write(fd, "halo dari sender — pesan 3");
    print("[pipe_sender] kirim pesan 3\n");

    print("[pipe_sender] selesai mengirim\n");
    exit();
}
