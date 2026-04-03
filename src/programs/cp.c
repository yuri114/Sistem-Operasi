#include "lib.h"

/* cp.c — Fondasi AB: salin file.
 * Penggunaan: cp <sumber> <tujuan> */

static char _argbuf[512];
static char *_argv[9];

void _start() {
    int argc = getargv(_argbuf, _argv);
    if (argc < 3) {
        print("Penggunaan: cp <sumber> <tujuan>\n");
        exit();
    }
    const char *src = _argv[1];
    const char *dst = _argv[2];

    /* Baca sumber */
    int src_fd = sys_open(src, VFS_O_RDONLY);
    if (src_fd < 0) {
        print("cp: tidak dapat membuka: "); print(src); print("\n");
        exit();
    }

    /* Buka/buat tujuan */
    int dst_fd = sys_open(dst, VFS_O_WRONLY | VFS_O_CREATE);
    if (dst_fd < 0) {
        print("cp: tidak dapat membuat: "); print(dst); print("\n");
        sys_close_fd(src_fd);
        exit();
    }

    /* Salin data */
    char buf[512];
    int n;
    while (1) {
        n = sys_read_fd(src_fd, buf, 512);
        if (n <= 0) break;
        sys_write_fd(dst_fd, buf, n);
    }
    sys_close_fd(src_fd);
    sys_close_fd(dst_fd);
    exit();
}
