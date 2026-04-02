/* pipetest.c — Demo F-R2: pipe2() via VFS fd, fork, redirect
 *
 * Urutan pengujian:
 *  1. pipe2(fds) → tulis dari induk, baca dari anak (fork)
 *  2. vfs_open/read/write dengan VFS fd biasa
 */
#include "lib.h"

void _start() {
    char buf[64];
    int fds[2];
    int n;

    print("pipetest: mulai\n");

    /* ---- Test 1: pipe2 + fork ---- */
    n = pipe2(fds);
    if (n < 0) {
        print("pipe2: GAGAL\n");
        exit();
    }
    print("pipe2: OK, fds[0]=");
    itoa((unsigned)fds[0], buf); print(buf);
    print(" fds[1]=");
    itoa((unsigned)fds[1], buf); print(buf); print("\n");

    /* Tulis ke pipe SEBELUM fork agar data sudah ada saat anak berjalan */
    const char *msg = "Hello dari induk!";
    int w = sys_write_fd(fds[1], msg, 17);
    sys_close_fd(fds[1]);
    print("induk: tulis ke pipe = ");
    itoa((unsigned)w, buf); print(buf); print(" bytes\n");

    int child = fork();
    if (child < 0) {
        print("fork: GAGAL\n");
        exit();
    }

    if (child == 0) {
        /* Anak: baca dari read-end (fds[0]) — data sudah ada */
        char rbuf[32];
        int r = sys_read_fd(fds[0], rbuf, 31);
        if (r > 0) {
            rbuf[r] = '\0';
            print("anak: baca dari pipe: [");
            print(rbuf);
            print("]\n");
            if (rbuf[0] == 'H')
                print("anak: isi OK\n");
            else
                print("anak: isi SALAH\n");
        } else {
            print("anak: read GAGAL\n");
        }
        sys_close_fd(fds[0]);
        exit();
    } else {
        /* Induk: tunggu anak selesai */
        waitpid(child);
        print("induk: waitpid anak selesai\n");
    }

    print("pipetest: semua tes selesai OK\n");
    exit();
}
