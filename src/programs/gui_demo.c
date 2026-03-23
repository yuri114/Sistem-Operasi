/*
 * gui_demo.c — Demo Window Manager
 *
 * Membuka 2 window, menampilkan teks, dan merespons event mouse.
 * Klik tombol X untuk menutup setiap window.
 * Program keluar ketika kedua window tertutup.
 *
 * Kompilasi: gcc -m32 -nostdlib -nostartfiles -fno-builtin -fno-pic \
 *              -T src/programs/user.ld src/programs/gui_demo.c -o build/gui_demo.elf
 */
#include "lib.h"

/* Konversi int ke string desimal di buffer */
static void int_to_s(int n, char *buf) {
    if (n == 0) { buf[0]='0'; buf[1]='\0'; return; }
    char tmp[12]; int i=0, neg=0;
    if (n < 0) { neg=1; n=-n; }
    while (n > 0) { tmp[i++]='0'+(n%10); n/=10; }
    if (neg) tmp[i++]='-';
    int j=0;
    while (i>0) buf[j++]=tmp[--i];
    buf[j]='\0';
}

void _start(void) {
    /* WM akan membersihkan layar secara otomatis saat window pertama dibuat */

    /* ---- Buat Window 1 (kiri atas) ---- */
    int w1 = win_create(40, 40, 260, 180, "Info Sistem");
    win_draw(w1,  6,  6, "MyOS Window Manager",     GFX_YELLOW, GFX_BLACK);
    win_draw(w1,  6, 20, "VBE 640x480 @ 8bpp",      GFX_WHITE,  GFX_BLACK);
    win_draw(w1,  6, 34, "PS/2 Mouse  + IRQ12",      GFX_WHITE,  GFX_BLACK);
    win_draw(w1,  6, 48, "Font 8x8  bitmap",         GFX_WHITE,  GFX_BLACK);
    win_draw(w1,  6, 70, "Drag: klik & tahan judul", GFX_LGRAY,  GFX_BLACK);
    win_draw(w1,  6, 84, "Tutup: klik tombol X",     GFX_LGRAY,  GFX_BLACK);

    /* ---- Buat Window 2 (kanan bawah) ---- */
    int w2 = win_create(330, 200, 220, 160, "Mouse Monitor");
    win_draw(w2,  6,  6, "Posisi mouse:", GFX_CYAN,  GFX_BLACK);
    win_draw(w2,  6, 30, "X: ---",        GFX_WHITE, GFX_BLACK);
    win_draw(w2,  6, 44, "Y: ---",        GFX_WHITE, GFX_BLACK);
    win_draw(w2,  6, 66, "Tombol:",       GFX_CYAN,  GFX_BLACK);
    win_draw(w2,  6, 80, "Kiri   = [ ]",  GFX_WHITE, GFX_BLACK);
    win_draw(w2,  6, 94, "Kanan  = [ ]",  GFX_WHITE, GFX_BLACK);

    int alive1 = 1, alive2 = 1;
    int tick = 0;

    while (alive1 || alive2) {
        /* Poll event window 1 */
        if (alive1) {
            int ev = win_poll(w1);
            if (ev == WIN_EVENT_CLOSE) {
                win_destroy(w1);
                alive1 = 0;
            }
        }

        /* Poll event window 2 */
        if (alive2) {
            int ev = win_poll(w2);
            if (ev == WIN_EVENT_CLOSE) {
                win_destroy(w2);
                alive2 = 0;
            }
        }

        /* Update tampilan mouse setiap ~10 tick */
        if (alive2 && (tick % 10 == 0)) {
            MouseState ms;
            mouse_get(&ms);

            char num[12];

            /* Update X */
            int_to_s(ms.x, num);
            /* Tulis label + angka (padding spasi agar hapus digit lama) */
            win_draw(w2, 6, 30, "X:       ", GFX_WHITE, GFX_BLACK);
            win_draw(w2, 6, 30, "X: ",       GFX_WHITE, GFX_BLACK);
            win_draw(w2, 24, 30, num,         GFX_LGREEN, GFX_BLACK);

            /* Update Y */
            int_to_s(ms.y, num);
            win_draw(w2, 6, 44, "Y:       ", GFX_WHITE, GFX_BLACK);
            win_draw(w2, 6, 44, "Y: ",       GFX_WHITE, GFX_BLACK);
            win_draw(w2, 24, 44, num,         GFX_LGREEN, GFX_BLACK);

            /* Update status tombol */
            win_draw(w2, 6, 80,
                     (ms.buttons & 0x01) ? "Kiri   = [X]" : "Kiri   = [ ]",
                     (ms.buttons & 0x01) ? GFX_LRED : GFX_WHITE, GFX_BLACK);
            win_draw(w2, 6, 94,
                     (ms.buttons & 0x02) ? "Kanan  = [X]" : "Kanan  = [ ]",
                     (ms.buttons & 0x02) ? GFX_LRED : GFX_WHITE, GFX_BLACK);
        }

        tick++;
        yield();
    }

    exit();
}
