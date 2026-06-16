/* sysinfo.c — System Information viewer (F4)
 * Menampilkan info sistem: uptime, PID, ticks, arsitektur.
 * Diperbarui setiap detik.
 */
#include "lib.h"

void _start() {
    int w = 360, h = 130;
    int x = (SCREEN_W - w) / 2;
    int y = (SCREEN_H - 48 - h) / 2 + 40;
    int id = win_create(x, y, w, h, "System Info");
    if (id < 0) exit();
    win_flush();

    unsigned int last_sec = (unsigned int)-1;
    char buf[80];
    int pid = getpid();

    while (1) {
        int ev = win_poll(id);
        if (WIN_EV_TYPE(ev) == WIN_EVENT_CLOSE) break;

        unsigned int ticks = get_ticks();
        unsigned int sec   = ticks / 1000;

        if (sec != last_sec) {
            last_sec = sec;
            win_clear(id, GFX_BLACK);

            /* Baris 1: PID */
            sprintf(buf, "PID      : %d", pid);
            win_draw(id, 8, 8, buf, GFX_WHITE, GFX_BLACK);

            /* Baris 2: Uptime */
            unsigned int hh = (sec / 3600) % 24;
            unsigned int mm = (sec / 60) % 60;
            unsigned int ss =  sec % 60;
            sprintf(buf, "Uptime   : %02d:%02d:%02d", hh, mm, ss);
            win_draw(id, 8, 24, buf, GFX_LGREEN, GFX_BLACK);

            /* Baris 3: Ticks */
            sprintf(buf, "Ticks    : %u", ticks);
            win_draw(id, 8, 40, buf, GFX_WHITE, GFX_BLACK);

            /* Baris 4: Timer */
            win_draw(id, 8, 56, "Timer    : 1000 Hz  (1 ms / tick)", GFX_LGRAY, GFX_BLACK);

            /* Baris 5: Arch */
            win_draw(id, 8, 72, "Arch     : x86_64 Long Mode (IA-32e)", GFX_LGRAY, GFX_BLACK);

            /* Baris 6: FS */
            win_draw(id, 8, 88, "FS       : MFS3  64 file x 64 KB", GFX_LGRAY, GFX_BLACK);

            /* Baris 7: Kernel size label */
            win_draw(id, 8, 104, "Kernel   : ~190 KB  |  Heap: 6 MB", GFX_DGRAY, GFX_BLACK);
        }
        yield();
    }

    win_destroy(id);
    exit();
}
