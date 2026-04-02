#include "lib.h"

/* polltest.c — Fondasi W: demo non-blocking fd + poll()
 *
 * Skenario:
 *   1. Buat anonymous pipe (pipe2).
 *   2. Buat thread penulis yang tidur 200 ms lalu menulis ke write-end.
 *   3. Main thread:
 *        a. Set read-end ke O_NONBLOCK via fcntl_setfl.
 *        b. Baca sebelum data tersedia — verifikasi -EAGAIN.
 *        c. Cabut O_NONBLOCK, poll() pada read-end dengan timeout 2000 ms.
 *        d. Setelah poll return POLLIN, baca data — verifikasi isi.
 *   Hasil: "SEMUA UJI LULUS" jika benar, exit_code(1) jika gagal. */

static int g_pipe_fds[2];   /* [0]=read, [1]=write */

/* ------------------------------------------------------------------ */
/* Thread penulis: tidur 200 ms, tulis "hello" ke write-end            */
/* ------------------------------------------------------------------ */
static void writer_fn(void *arg) {
    (void)arg;
    sleep_ms(200);
    const char *msg = "hello";
    int len = 5;
    sys_write_fd(g_pipe_fds[1], msg, len);
    thread_exit();
}

/* ------------------------------------------------------------------ */
void _start() {
    char buf[64];
    char tmp[16];

    print("[polltest] buat pipe...\n");
    if (pipe2(g_pipe_fds) < 0) {
        print("[polltest] GAGAL: pipe2 error\n");
        exit_code(1);
    }

    /* --- Uji 1: set O_NONBLOCK, baca saat pipe kosong → -EAGAIN --- */
    fcntl_setfl(g_pipe_fds[0], O_NONBLOCK);
    int r = sys_read_fd(g_pipe_fds[0], buf, (int)sizeof(buf));
    if (r == -EAGAIN) {
        print("[polltest] Uji 1 LULUS: -EAGAIN saat pipe kosong + O_NONBLOCK\n");
    } else {
        print("[polltest] Uji 1 GAGAL: harusnya -EAGAIN, dapat ");
        itoa((unsigned int)(int)r, tmp); print(tmp); print("\n");
        exit_code(1);
    }

    /* Cabut O_NONBLOCK agar poll dapat memblok benar */
    fcntl_setfl(g_pipe_fds[0], 0x01);  /* 0x01 = VFS_O_RDONLY */

    /* --- Buat thread penulis --- */
    print("[polltest] buat writer thread (tulis setelah 200 ms)...\n");
    int tid = thread_create(writer_fn, 0);
    if (tid < 0) {
        print("[polltest] GAGAL: thread_create error\n");
        exit_code(1);
    }

    /* --- Uji 2: poll() dengan timeout 2000 ms --- */
    PollFd pfds[1];
    pfds[0].fd      = g_pipe_fds[0];
    pfds[0].events  = POLLIN;
    pfds[0].revents = 0;

    print("[polltest] poll() menunggu POLLIN, timeout 2000 ms...\n");
    int np = poll(pfds, 1, 2000);
    if (np > 0 && (pfds[0].revents & POLLIN)) {
        print("[polltest] Uji 2 LULUS: POLLIN terdeteksi\n");
    } else if (np == 0) {
        print("[polltest] Uji 2 GAGAL: timeout habis\n");
        exit_code(1);
    } else {
        print("[polltest] Uji 2 GAGAL: np=");
        itoa((unsigned int)(int)np, tmp); print(tmp);
        print(" revents=");
        itoa((unsigned int)(int)pfds[0].revents, tmp); print(tmp);
        print("\n");
        exit_code(1);
    }

    /* --- Uji 3: baca data dari pipe setelah POLLIN --- */
    int n = sys_read_fd(g_pipe_fds[0], buf, (int)(sizeof(buf) - 1));
    if (n > 0) {
        buf[n] = '\0';
        print("[polltest] Uji 3 LULUS: data = '");
        print(buf);
        print("'\n");
    } else {
        print("[polltest] Uji 3 GAGAL: read gagal setelah POLLIN, n=");
        itoa((unsigned int)(int)n, tmp); print(tmp); print("\n");
        exit_code(1);
    }

    thread_join(tid);

    print("[polltest] SEMUA UJI LULUS\n");
    exit_code(0);
}
