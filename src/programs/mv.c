#include "lib.h"

/* mv.c — Fondasi AB: pindah/rename file.
 * Penggunaan: mv <lama> <baru> */

static char _argbuf[512];
static char *_argv[9];

/* Gunakan SYS_MFS4_RENAME syscall */
#ifndef SYS_MFS4_RENAME
#define SYS_MFS4_RENAME 92
#endif

static inline int fs_rename(const char *old_path, const char *new_path) {
    return (int)syscall2(SYS_MFS4_RENAME, (long)old_path, (long)new_path);
}

void _start() {
    int argc = getargv(_argbuf, _argv);
    if (argc < 3) {
        print("Penggunaan: mv <lama> <baru>\n");
        exit();
    }
    int r = fs_rename(_argv[1], _argv[2]);
    if (r < 0) {
        print("mv: gagal: "); print(_argv[1]); print(" -> "); print(_argv[2]); print("\n");
    }
    exit();
}
