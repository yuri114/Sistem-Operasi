#include "lib.h"

/* wc.c — Fondasi AB: hitung baris/kata/karakter dari stdin atau file.
 * Penggunaan: wc [file ...]
 * Output: <baris> <kata> <char> [nama_file] */

static char _argbuf[512];
static char *_argv[9];

static void wc_count(int fd, const char *name) {
    char buf[512];
    long lines = 0, words = 0, chars = 0;
    int in_word = 0;
    int n;
    while (1) {
        n = sys_read_fd(fd, buf, 511);
        if (n <= 0) break;
        int i;
        for (i = 0; i < n; i++) {
            char c = buf[i];
            chars++;
            if (c == '\n') lines++;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                in_word = 0;
            } else {
                if (!in_word) { words++; in_word = 1; }
            }
        }
    }
    char tmp[24];
    /* print lines */
    itoa((unsigned int)lines, tmp); print(tmp); print(" ");
    itoa((unsigned int)words, tmp); print(tmp); print(" ");
    itoa((unsigned int)chars, tmp); print(tmp);
    if (name) { print(" "); print(name); }
    print("\n");
}

void _start() {
    int argc = getargv(_argbuf, _argv);
    if (argc <= 1) {
        wc_count(0, 0);
    } else {
        int i;
        for (i = 1; i < argc; i++) {
            int fd = sys_open(_argv[i], VFS_O_RDONLY);
            if (fd < 0) { print("wc: tidak dapat membuka: "); print(_argv[i]); print("\n"); continue; }
            wc_count(fd, _argv[i]);
            sys_close_fd(fd);
        }
    }
    exit();
}
