#include "lib.h"

/* sort.c — Fondasi AZ: urutkan baris teks secara alfabet.
 * Penggunaan: sort [file ...]  atau  cmd | sort */

static char _argbuf[512];
static char *_argv[9];

#define SORT_MAX_LINES 512
#define SORT_LINE_LEN  128

static char lines[SORT_MAX_LINES][SORT_LINE_LEN];
static int  nlines = 0;

/* Baca semua baris dari fd ke buffer lines[] */
static void read_lines(int fd) {
    char buf[4096];
    int n;
    static char leftover[SORT_LINE_LEN];
    int lo = 0;

    while (nlines < SORT_MAX_LINES) {
        n = sys_read_fd(fd, buf, 4095);
        if (n <= 0) break;
        buf[n] = '\0';
        int i = 0;
        while (i < n && nlines < SORT_MAX_LINES) {
            if (buf[i] == '\n') {
                leftover[lo] = '\0';
                int j;
                for (j = 0; j < lo && j < SORT_LINE_LEN - 1; j++)
                    lines[nlines][j] = leftover[j];
                lines[nlines][j] = '\0';
                nlines++;
                lo = 0;
            } else {
                if (lo < SORT_LINE_LEN - 1) leftover[lo++] = buf[i];
            }
            i++;
        }
    }
    /* flush leftover */
    if (lo > 0 && nlines < SORT_MAX_LINES) {
        leftover[lo] = '\0';
        int j;
        for (j = 0; j < lo && j < SORT_LINE_LEN - 1; j++)
            lines[nlines][j] = leftover[j];
        lines[nlines][j] = '\0';
        nlines++;
    }
}

void _start() {
    int argc = getargv(_argbuf, _argv);
    nlines = 0;

    if (argc <= 1) {
        read_lines(0);
    } else {
        int i;
        for (i = 1; i < argc; i++) {
            int fd = sys_open(_argv[i], VFS_O_RDONLY);
            if (fd < 0) {
                print("sort: tidak dapat membuka: "); print(_argv[i]); print("\n");
                continue;
            }
            read_lines(fd);
            sys_close_fd(fd);
        }
    }

    /* Bubble sort */
    int a, b;
    char tmp[SORT_LINE_LEN];
    for (a = 0; a < nlines - 1; a++) {
        for (b = 0; b < nlines - 1 - a; b++) {
            /* Compare lines[b] vs lines[b+1] */
            int k = 0, cmp = 0;
            while (lines[b][k] && lines[b+1][k]) {
                if (lines[b][k] != lines[b+1][k]) {
                    cmp = (unsigned char)lines[b][k] - (unsigned char)lines[b+1][k];
                    break;
                }
                k++;
            }
            if (cmp == 0) cmp = (unsigned char)lines[b][k] - (unsigned char)lines[b+1][k];
            if (cmp > 0) {
                int t;
                for (t = 0; t < SORT_LINE_LEN; t++) tmp[t]       = lines[b][t];
                for (t = 0; t < SORT_LINE_LEN; t++) lines[b][t]   = lines[b+1][t];
                for (t = 0; t < SORT_LINE_LEN; t++) lines[b+1][t] = tmp[t];
            }
        }
    }

    /* Print hasil */
    int i;
    for (i = 0; i < nlines; i++) {
        print(lines[i]);
        print("\n");
    }
    exit();
}
