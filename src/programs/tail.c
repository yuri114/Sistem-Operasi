#include "lib.h"

/* tail.c — tampilkan N baris terakhir dari stdin atau file.
 * Penggunaan: tail [-n N] [file ...]
 * Default: 10 baris. */

static char _argbuf[512];
static char *_argv[9];

#define TAIL_MAX_LINES 512
#define TAIL_LINE_LEN  256

/* Ring buffer untuk menyimpan N baris terakhir */
static char ring[TAIL_MAX_LINES][TAIL_LINE_LEN];
static int  rhead = 0; /* indeks tulis berikutnya (modulo) */
static int  rcount = 0;

static void tail_print(int fd, int nlines) {
    rhead = 0; rcount = 0;
    if (nlines > TAIL_MAX_LINES) nlines = TAIL_MAX_LINES;

    /* Baca semua data, simpan baris ke ring buffer */
    static char leftover[TAIL_LINE_LEN];
    int lo = 0;
    char buf[1024];
    int n;
    while (1) {
        n = sys_read_fd(fd, buf, 1023);
        if (n <= 0) break;
        buf[n] = '\0';
        int i;
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                leftover[lo] = '\0';
                int k;
                for (k = 0; k < lo && k < TAIL_LINE_LEN - 1; k++)
                    ring[rhead][k] = leftover[k];
                ring[rhead][k] = '\0';
                rhead = (rhead + 1) % TAIL_MAX_LINES;
                if (rcount < TAIL_MAX_LINES) rcount++;
                lo = 0;
            } else {
                if (lo < TAIL_LINE_LEN - 1) leftover[lo++] = buf[i];
            }
        }
    }
    /* flush leftover tanpa newline */
    if (lo > 0) {
        leftover[lo] = '\0';
        int k;
        for (k = 0; k < lo && k < TAIL_LINE_LEN - 1; k++)
            ring[rhead][k] = leftover[k];
        ring[rhead][k] = '\0';
        rhead = (rhead + 1) % TAIL_MAX_LINES;
        if (rcount < TAIL_MAX_LINES) rcount++;
        lo = 0;
    }

    /* Cetak nlines terakhir dari ring buffer */
    int to_print = rcount < nlines ? rcount : nlines;
    int start = (rhead - to_print + TAIL_MAX_LINES * 2) % TAIL_MAX_LINES;
    int i;
    for (i = 0; i < to_print; i++) {
        int idx = (start + i) % TAIL_MAX_LINES;
        print(ring[idx]);
        print("\n");
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
        tail_print(0, nlines);
    } else {
        int i;
        for (i = first_file; i < argc; i++) {
            if (argc - first_file > 1) { print("==> "); print(_argv[i]); print(" <==\n"); }
            int fd = sys_open(_argv[i], VFS_O_RDONLY);
            if (fd < 0) { print("tail: tidak dapat membuka: "); print(_argv[i]); print("\n"); continue; }
            tail_print(fd, nlines);
            sys_close_fd(fd);
        }
    }
    exit();
}
