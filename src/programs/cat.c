#include "lib.h"

/* cat.c — Fondasi AB: tampilkan isi file atau stdin.
 * Penggunaan: cat [file ...]
 * Tanpa argumen: baca stdin dan cetak ke stdout.
 * Dengan argumen: baca setiap file dan cetak. */

static char _argbuf[512];
static char *_argv[9];

static void cat_fd(int fd) {
    char buf[512];
    int n;
    while (1) {
        n = sys_read_fd(fd, buf, 511);
        if (n <= 0) break;
        buf[n] = '\0';
        print(buf);
    }
}

static void cat_file(const char *name) {
    int fd = sys_open(name, VFS_O_RDONLY);
    if (fd < 0) {
        print("cat: tidak dapat membuka: ");
        print(name); print("\n");
        return;
    }
    cat_fd(fd);
    sys_close_fd(fd);
}

void _start() {
    int argc = getargv(_argbuf, _argv);
    if (argc <= 1) {
        /* Baca dari stdin */
        cat_fd(0);
    } else {
        int i;
        for (i = 1; i < argc; i++)
            cat_file(_argv[i]);
    }
    exit();
}
