#include "lib.h"

/* head.c — Fondasi AB: tampilkan N baris pertama dari stdin atau file.
 * Penggunaan: head [-n N] [file ...]
 * Default: 10 baris. */

static char _argbuf[512];
static char *_argv[9];

static void head_print(int fd, int nlines) {
    char buf[512];
    int lines = 0, pos = 0, n;
    int done = 0;
    while (!done) {
        n = sys_read_fd(fd, buf + pos, 511 - pos);
        if (n <= 0) {
            if (pos > 0) { buf[pos] = '\0'; print(buf); }
            break;
        }
        buf[pos + n] = '\0';
        int i;
        for (i = 0; buf[i]; i++) {
            if (buf[i] == '\n') {
                buf[i+1] = '\0';
                print(buf);
                lines++;
                if (lines >= nlines) { done = 1; break; }
                /* shift remaining */
                int r = 0;
                while (buf[i+1+r]) { buf[r] = buf[i+1+r]; r++; }
                buf[r] = '\0'; pos = r; i = -1;
                break;
            }
        }
        if (!done) pos = strlen(buf);
    }
}

void _start() {
    int argc = getargv(_argbuf, _argv);
    int nlines = 10;
    int first_file = 1;

    /* Parse -n N */
    if (argc >= 3 && _argv[1][0] == '-' && _argv[1][1] == 'n') {
        nlines = atoi(_argv[2]);
        if (nlines <= 0) nlines = 10;
        first_file = 3;
    }

    if (argc <= first_file) {
        head_print(0, nlines);
    } else {
        int i;
        for (i = first_file; i < argc; i++) {
            if (argc - first_file > 1) { print("==> "); print(_argv[i]); print(" <==\n"); }
            int fd = sys_open(_argv[i], VFS_O_RDONLY);
            if (fd < 0) { print("head: tidak dapat membuka: "); print(_argv[i]); print("\n"); continue; }
            head_print(fd, nlines);
            sys_close_fd(fd);
        }
    }
    exit();
}
