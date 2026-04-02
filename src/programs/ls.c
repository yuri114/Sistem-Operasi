#include "lib.h"

/* ls.c — Fondasi S: user-space ls menggunakan syscall SYS_FS_LIST.
 * Keluaran: satu nama file per baris ke stdout.
 * Cocok digunakan dalam pipeline: exec ls | grep */
void _start() {
    char buf[2048];
    int cnt = fs_list(buf, sizeof(buf));
    if (cnt <= 0) {
        print("(kosong)\n");
        exit();
    }
    /* fs_list mengisi buf dengan "nama\nnama\n..." diakhiri '\0'.
     * Cukup print seluruh buffer karena setiap nama sudah diakhiri '\n'. */
    print(buf);
    exit();
}
