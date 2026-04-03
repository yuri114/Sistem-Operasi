#include "lib.h"

/* grep.c — Fondasi AA: filter stdin ke stdout dengan pola dari argv[1].
 * Penggunaan: exec grep <pola>
 *         atau pipeline: ls | grep elf
 * Menampilkan semua baris yang mengandung pola. */

/* Cari apakah haystack mengandung needle. Return 1 jika ya, 0 jika tidak. */
static int contains(const char *haystack, const char *needle) {
    int ni = 0, hi = 0;
    if (!needle[0]) return 1;           /* pola kosong cocok semua */
    while (haystack[hi]) {
        if (haystack[hi] == needle[ni]) {
            int hi2 = hi, ni2 = ni;
            while (haystack[hi2] && needle[ni2] && haystack[hi2] == needle[ni2]) {
                hi2++; ni2++;
            }
            if (!needle[ni2]) return 1; /* kecocokan penuh */
        }
        hi++;
    }
    return 0;
}

static char _argbuf[512];
static char *_argv[9];

void _start() {
    int argc = getargv(_argbuf, _argv);
    const char *pattern = (argc >= 2 && _argv[1] && _argv[1][0]) ? _argv[1] : "";
    char line[256];
    int n;

    while (1) {
        n = sys_read_fd(0, line, 255);
        if (n <= 0) break;              /* EOF atau error */
        line[n] = '\0';
        if (contains(line, pattern)) {
            print(line);
            print("\n");
        }
    }
    exit();
}
