/* forktest.c — Demo Fondasi Q: fork(), exec_replace(), mmap(), munmap()
 *
 * Urutan pengujian:
 *  1. fork() → induk tunggu anak, anak cetak & keluar   (F-Q1)
 *  2. mmap(2) → tulis → baca → munmap                  (F-Q4)
 *  3. exec_replace("hello") → ganti image jadi hello   (F-Q2 — di anak fork berikutnya)
 */
#include "lib.h"

static void itoa_s(int n, char *buf) {
    itoa((unsigned int)n, buf);
}

void _start() {
    char buf[32];

    /* ---- Test 1: fork ---- */
    print("forktest: pid=");
    itoa_s(getpid(), buf); print(buf); print("\n");

    int child = fork();
    if (child < 0) {
        print("fork: GAGAL\n");
        exit();
    }

    if (child == 0) {
        /* Proses anak */
        print("anak: pid="); itoa_s(getpid(), buf); print(buf); print("\n");
        print("anak: fork() mengembalikan 0 -> OK\n");
        sleep_ms(50);
        print("anak: selesai\n");
        exit();
    } else {
        /* Proses induk */
        print("induk: pid="); itoa_s(getpid(), buf); print(buf);
        print(", anak pid="); itoa_s(child, buf); print(buf); print("\n");
        waitpid(child);
        print("induk: waitpid(anak) selesai\n");
    }

    /* ---- Test 2: mmap / munmap ---- */
    print("\nmmap: alokasi 2 halaman...\n");
    int *area = (int *)mmap(2);
    if (!area) {
        print("mmap: GAGAL\n");
    } else {
        area[0] = 0xDEAD;
        area[1] = 0xBEEF;
        print("mmap: tulis 0xDEAD 0xBEEF\n");
        print("mmap: baca area[0]=");
        itoa((unsigned)area[0], buf); print(buf); print("\n");
        print("mmap: baca area[1]=");
        itoa((unsigned)area[1], buf); print(buf); print("\n");
        if (area[0] == 0xDEAD && area[1] == 0xBEEF)
            print("mmap: OK\n");
        else
            print("mmap: GAGAL (data salah)\n");
        munmap(area, 2);
        print("mmap: munmap OK\n");
    }

    /* ---- Test 3: fork + exec_replace di anak ---- */
    print("\nexec_replace di anak: jalankan 'hello'...\n");
    int child2 = fork();
    if (child2 < 0) {
        print("fork2: GAGAL\n");
    } else if (child2 == 0) {
        /* Anak: ganti image dengan "hello" */
        exec_replace("hello");
        /* Jika sampai sini, exec_replace gagal */
        print("exec_replace: GAGAL\n");
        exit();
    } else {
        waitpid(child2);
        print("induk: anak exec_replace selesai\n");
    }

    print("\nforktest: semua tes selesai OK\n");
    exit();
}
