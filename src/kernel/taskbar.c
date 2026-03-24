/*
 * taskbar.c — Taskbar tetap di bawah layar (y = SCREEN_H - TASKBAR_H_PX)
 *
 * Menampilkan:
 *  - Tombol per window yang sedang hidup (klik → raise window)
 *  - Clock HH:MM:SS di kanan (berdasarkan PIT ticks @ 100Hz)
 */
#include "taskbar.h"
#include "graphics.h"
#include "timer.h"
#include "window.h"   /* wm_get_z_count, wm_get_z_id, wm_get_title, wm_raise_by_id */
#include <stdint.h>

#define TASKBAR_Y     (SCREEN_H - TASKBAR_H_PX)
#define CLOCK_W       72    /* lebar area jam: 9 karakter × 8px */
#define BTN_MARGIN    2
#define TIMER_HZ      100   /* timer_init(100) di kernel.c */

/* ------------------------------------------------------------------ */
/* Render clock HH:MM:SS                                               */
/* ------------------------------------------------------------------ */
static void draw_clock(void) {
    uint32_t t   = get_ticks() / TIMER_HZ;
    uint32_t sec = t % 60;
    uint32_t mn  = (t / 60) % 60;
    uint32_t hr  = (t / 3600) % 24;

    char buf[9];
    buf[0] = '0' + hr / 10; buf[1] = '0' + hr % 10;
    buf[2] = ':';
    buf[3] = '0' + mn / 10; buf[4] = '0' + mn % 10;
    buf[5] = ':';
    buf[6] = '0' + sec / 10; buf[7] = '0' + sec % 10;
    buf[8] = '\0';

    int cx = SCREEN_W - CLOCK_W;
    fill_rect(cx, TASKBAR_Y, CLOCK_W, TASKBAR_H_PX, GFX_DGRAY);
    draw_string_gfx(cx + 4, TASKBAR_Y + (TASKBAR_H_PX - 8) / 2,
                    buf, GFX_WHITE, GFX_DGRAY);
}

/* ------------------------------------------------------------------ */
/* Hitung posisi tombol window ke-i                                    */
/* ------------------------------------------------------------------ */
static int btn_area_w(void) { return SCREEN_W - CLOCK_W; }

static int btn_x(int idx, int total) {
    if (total == 0) return 0;
    int aw = btn_area_w();
    int bw = (aw - BTN_MARGIN) / total - BTN_MARGIN;
    return BTN_MARGIN + idx * (bw + BTN_MARGIN);
}

static int btn_w(int total) {
    if (total == 0) return 0;
    int aw = btn_area_w();
    return (aw - BTN_MARGIN) / total - BTN_MARGIN;
}

/* ------------------------------------------------------------------ */
/* Render                                                              */
/* ------------------------------------------------------------------ */
void taskbar_draw(void) {
    /* Latar belakang taskbar */
    fill_rect(0, TASKBAR_Y, SCREEN_W, TASKBAR_H_PX, GFX_DGRAY);

    /* Garis pemisah atas */
    fill_rect(0, TASKBAR_Y, SCREEN_W, 1, GFX_LGRAY);

    int total = wm_get_z_count();
    if (total > 0) {
        int bw = btn_w(total);
        int by = TASKBAR_Y + BTN_MARGIN;
        int bh = TASKBAR_H_PX - 2 * BTN_MARGIN;

        for (int i = 0; i < total; i++) {
            int id = wm_get_z_id(i);
            int bx = btn_x(i, total);

            /* Focused window (paling atas z-order) = warna terang */
            int focused = (i == total - 1);
            uint8_t bg = focused ? GFX_BLUE : GFX_LGRAY;
            uint8_t fg = focused ? GFX_WHITE : GFX_BLACK;

            fill_rect(bx, by, bw, bh, bg);
            /* Border 1px */
            fill_rect(bx, by, bw, 1, GFX_WHITE);
            fill_rect(bx, by, 1, bh, GFX_WHITE);
            fill_rect(bx + bw - 1, by, 1, bh, GFX_DGRAY);
            fill_rect(bx, by + bh - 1, bw, 1, GFX_DGRAY);

            /* Label: judul window, maks sesuai lebar tombol */
            const char *title = wm_get_title(id);
            int lx = bx + 2;
            int ly = by + (bh - 8) / 2;
            int end_x = bx + bw - 2;
            for (int k = 0; title[k] && lx + 8 <= end_x; k++, lx += 8)
                draw_char_gfx(lx, ly, title[k], fg, bg);
        }
    }

    draw_clock();
}

/* ------------------------------------------------------------------ */
/* Klik di area taskbar                                                */
/* ------------------------------------------------------------------ */
void taskbar_click(int mx, int my) {
    if (my < TASKBAR_Y || my >= SCREEN_H) return;

    /* Area jam tidak interaktif */
    if (mx >= SCREEN_W - CLOCK_W) return;

    int total = wm_get_z_count();
    if (total == 0) return;

    int bw = btn_w(total);
    for (int i = 0; i < total; i++) {
        int bx = btn_x(i, total);
        if (mx >= bx && mx < bx + bw) {
            wm_raise_by_id(wm_get_z_id(i));
            return;
        }
    }
}
