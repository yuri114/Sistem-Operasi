/* clock.c — Jam digital real-time berbasis ticks kernel (F4)
 * Menampilkan waktu sejak boot dalam format HH:MM:SS di window GUI.
 * Timer kernel 1000 Hz → 1 tick = 1 ms → 1000 ticks = 1 detik.
 */
#include "lib.h"

void _start() {
    int w = 240, h = 70;
    int x = (SCREEN_W - w) / 2;
    int y = (SCREEN_H - 48 - h) / 2;
    int id = win_create(x, y, w, h, "Uptime Clock");
    if (id < 0) exit();
    win_flush();

    unsigned int last_sec = (unsigned int)-1;
    char timebuf[16];

    while (1) {
        int ev = win_poll(id);
        if (WIN_EV_TYPE(ev) == WIN_EVENT_CLOSE) break;

        unsigned int ticks = get_ticks();
        unsigned int sec   = ticks / 1000;

        if (sec != last_sec) {
            last_sec = sec;
            unsigned int hh = (sec / 3600) % 24;
            unsigned int mm = (sec / 60) % 60;
            unsigned int ss =  sec % 60;

            /* Format "HH:MM:SS" manual (8 karakter) */
            timebuf[0] = '0' + hh / 10;
            timebuf[1] = '0' + hh % 10;
            timebuf[2] = ':';
            timebuf[3] = '0' + mm / 10;
            timebuf[4] = '0' + mm % 10;
            timebuf[5] = ':';
            timebuf[6] = '0' + ss / 10;
            timebuf[7] = '0' + ss % 10;
            timebuf[8] = '\0';

            win_clear(id, GFX_BLACK);
            /* Tengah: konten 240-2=238 px, string 8×8=64 px → x offset = (238-64)/2 = 87 */
            win_draw(id, 87, 20, timebuf, GFX_LGREEN, GFX_BLACK);

            /* Baris kecil bawah: label */
            win_draw(id, 60, 44, "uptime hh:mm:ss", GFX_DGRAY, GFX_BLACK);
        }
        yield();
    }

    win_destroy(id);
    exit();
}
