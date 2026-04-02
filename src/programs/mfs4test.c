/* mfs4test.c — Demo F-R3: MFS4 inode layer
 *
 * Urutan pengujian:
 *  1. mfs4_mkdir() — buat direktori baru
 *  2. mfs4_stat() pada file yang ada
 *  3. mfs4_symlink() + stat symlink
 *  4. mfs4_hardlink()
 *  5. mfs4_listdir()
 *  6. mfs4_unlink()
 */
#include "lib.h"

void _start() {
    char buf[128];
    int r;
    UMfs4Stat st;

    print("mfs4test: mulai\n");

    /* ---- Test 1: mkdir ---- */
    r = mfs4_mkdir("testdir");
    print("mkdir testdir: ");
    if (r == 0) print("OK\n");
    else { itoa((unsigned)r, buf); print("GAGAL ("); print(buf); print(")\n"); }

    /* ---- Test 2: stat file yang ada (hello) ---- */
    r = mfs4_stat("hello", &st);
    print("stat hello: ");
    if (r == 0) {
        print("OK type=");
        itoa((unsigned)st.type, buf); print(buf);
        print(" size=");
        itoa(st.size, buf); print(buf); print("\n");
    } else {
        print("tidak ditemukan (perlu ada file 'hello')\n");
    }

    /* ---- Test 3: symlink ---- */
    r = mfs4_symlink("hello_link", "hello");
    print("symlink hello_link->hello: ");
    if (r == 0) print("OK\n");
    else { itoa((unsigned)r, buf); print("GAGAL ("); print(buf); print(")\n"); }

    /* stat symlink (harus resolve ke hello) */
    r = mfs4_stat("hello_link", &st);
    print("stat hello_link (via symlink): ");
    if (r == 0) {
        print("OK type=");
        itoa((unsigned)st.type, buf); print(buf); print("\n");
    } else print("GAGAL\n");

    /* ---- Test 4: hardlink ---- */
    r = mfs4_hardlink("hello_hard", "hello");
    print("hardlink hello_hard->hello: ");
    if (r == 0) print("OK\n");
    else { itoa((unsigned)r, buf); print("GAGAL ("); print(buf); print(")\n"); }

    /* ---- Test 5: listdir root ---- */
    r = mfs4_listdir("", buf, (int)sizeof(buf));
    print("listdir root (");
    itoa((unsigned)r, buf + 64); print(buf + 64);
    print(" entries): ");
    if (r > 0) {
        /* buf berisi entri dipisah '\0', cetak semua */
        int i = 0, printed = 0;
        while (i < (int)sizeof(buf) && printed < r) {
            if (buf[i]) {
                print("["); print(&buf[i]); print("] ");
                while (buf[i]) i++;
                printed++;
            }
            i++;
        }
        print("\n");
    } else {
        print("(kosong atau GAGAL)\n");
    }

    /* ---- Test 6: unlink symlink ---- */
    r = mfs4_unlink("hello_link");
    print("unlink hello_link: ");
    if (r == 0) print("OK\n");
    else { itoa((unsigned)r, buf); print("GAGAL ("); print(buf); print(")\n"); }

    print("mfs4test: semua tes selesai\n");
    exit();
}
