#include "lib.h"

/* grep.c — Fondasi S: filter stdin ke stdout.
 * Membaca baris dari stdin (fd 0) sampai EOF, mencetak setiap baris
 * yang mengandung string "elf" (demo pipeline ls | grep).
 * Ketika argv didukung, pola pencarian akan diteruskan lewat argumen. */

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

void _start() {
    char line[256];
    /* Pola hardcode "test" untuk demo 'exec ls | grep' — menampilkan
     * file yang namanya mengandung "test" (pipetest, mfs4test, dll).
     * Ganti dengan argv saat dukungan argumen tersedia. */
    const char *pattern = "test";
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
