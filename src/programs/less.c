#include "lib.h"

/* less.c — pager sederhana: tampilkan isi file layar demi layar.
 * Penggunaan: less [file ...]
 * Tombol: SPACE = halaman berikutnya, q = keluar, ENTER = satu baris */

static char _argbuf[512];
static char *_argv[10];

#define LESS_MAX_LINES  2048
#define LESS_LINE_LEN   256
#define LESS_PAGE_LINES 48

static char lines[LESS_MAX_LINES][LESS_LINE_LEN];
static int  line_count = 0;

static void less_load(int fd) {
    line_count = 0;
    static char leftover[LESS_LINE_LEN];
    int lo = 0;
    char buf[1024];
    int n;
    while (line_count < LESS_MAX_LINES - 1) {
        n = sys_read_fd(fd, buf, 1023);
        if (n <= 0) break;
        int i;
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                leftover[lo] = '\0';
                int k;
                for (k = 0; k < lo && k < LESS_LINE_LEN - 1; k++)
                    lines[line_count][k] = leftover[k];
                lines[line_count][k] = '\0';
                line_count++;
                lo = 0;
                if (line_count >= LESS_MAX_LINES - 1) break;
            } else {
                if (lo < LESS_LINE_LEN - 1) leftover[lo++] = buf[i];
            }
        }
    }
    /* flush leftover tanpa newline */
    if (lo > 0) {
        leftover[lo] = '\0';
        int k;
        for (k = 0; k < lo && k < LESS_LINE_LEN - 1; k++)
            lines[line_count][k] = leftover[k];
        lines[line_count][k] = '\0';
        line_count++;
    }
}

static void less_show(const char *filename) {
    int fd = -1;
    if (filename) {
        fd = sys_open(filename, VFS_O_RDONLY);
        if (fd < 0) {
            print("less: cannot open ");
            print(filename);
            print("\n");
            return;
        }
    } else {
        fd = 0; /* stdin */
    }

    less_load(fd);
    if (filename) sys_close_fd(fd);

    if (line_count == 0) return;

    int pos = 0;
    while (pos < line_count) {
        /* Cetak satu halaman */
        int end = pos + LESS_PAGE_LINES;
        if (end > line_count) end = line_count;
        int i;
        for (i = pos; i < end; i++) {
            print(lines[i]);
            print("\n");
        }
        pos = end;

        if (pos >= line_count) break;

        /* Status bar */
        print("-- Baris ");
        print_int(pos);
        print("/");
        print_int(line_count);
        print(" -- [SPACE=lanjut, q=keluar] ");

        /* Tunggu input */
        while (1) {
            char c = getkey();
            if (c == 'q' || c == 'Q') {
                print("\n");
                return;
            }
            if (c == ' ') { print("\n"); break; }
            if (c == '\n') {
                /* Maju satu baris */
                print("\n");
                pos = end - LESS_PAGE_LINES + 1;
                break;
            }
        }
    }
    print("(END)\n");
}

void _start() {
    int argc = getargv(_argbuf, _argv);

    if (argc <= 1) {
        less_show(0);
    } else {
        int i;
        for (i = 1; i < argc; i++) {
            if (argc > 2) {
                print("==> ");
                print(_argv[i]);
                print(" <==\n");
            }
            less_show(_argv[i]);
        }
    }
    exit(0);
}
