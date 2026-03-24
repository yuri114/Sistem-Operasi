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
#include "syscall.h"  /* kernel_exec */
#include <stdint.h>

#define TASKBAR_Y     (SCREEN_H - TASKBAR_H_PX)
#define CLOCK_W       72    /* lebar area jam: 9 karakter × 8px */
#define BTN_MARGIN    2
#define TIMER_HZ      100   /* timer_init(100) di kernel.c */

/* Quick-launch buttons di sisi kiri taskbar */
#define LAUNCH_BTN_W  40
#define LAUNCH_BTN_H  (TASKBAR_H_PX - 2 * BTN_MARGIN)
#define LAUNCH_BTN_Y  (TASKBAR_Y + BTN_MARGIN)

static const char *launch_labels[] = { "Paint", "Calc", "Note", "Files", "Term" };
static const char *launch_names[]  = { "paint", "calc", "notepad", "filemanager", "gui_term" };
#define LAUNCH_COUNT  5
#define LAUNCH_W      (LAUNCH_COUNT * LAUNCH_BTN_W + BTN_MARGIN)  /* total lebar area launch */

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
static int btn_area_w(void) { return SCREEN_W - CLOCK_W - LAUNCH_W; }

static int btn_x(int idx, int total) {
    if (total == 0) return 0;
    int aw = btn_area_w();
    int bw = (aw - BTN_MARGIN) / total - BTN_MARGIN;
    return LAUNCH_W + BTN_MARGIN + idx * (bw + BTN_MARGIN);
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

    /* --- Quick-launch buttons (kiri) --- */
    for (int i = 0; i < LAUNCH_COUNT; i++) {
        int bx = BTN_MARGIN + i * LAUNCH_BTN_W;
        fill_rect(bx, LAUNCH_BTN_Y, LAUNCH_BTN_W - BTN_MARGIN, LAUNCH_BTN_H, GFX_LGRAY);
        fill_rect(bx,                          LAUNCH_BTN_Y, LAUNCH_BTN_W - BTN_MARGIN, 1, GFX_WHITE);
        fill_rect(bx,                          LAUNCH_BTN_Y, 1, LAUNCH_BTN_H, GFX_WHITE);
        fill_rect(bx + LAUNCH_BTN_W - BTN_MARGIN - 1, LAUNCH_BTN_Y, 1, LAUNCH_BTN_H, GFX_DGRAY);
        fill_rect(bx,                          LAUNCH_BTN_Y + LAUNCH_BTN_H - 1, LAUNCH_BTN_W - BTN_MARGIN, 1, GFX_DGRAY);
        const char *lbl = launch_labels[i];
        int lx = bx + 2;
        int ly = LAUNCH_BTN_Y + (LAUNCH_BTN_H - 8) / 2;
        for (int k = 0; lbl[k] && lx + 8 <= bx + LAUNCH_BTN_W - 2; k++, lx += 8)
            draw_char_gfx(lx, ly, lbl[k], GFX_BLACK, GFX_LGRAY);
    }
    /* Garis pemisah antara launch area dan window buttons */
    fill_rect(LAUNCH_W - 1, TASKBAR_Y + 1, 1, TASKBAR_H_PX - 2, GFX_BLACK);

    int total = wm_get_z_count();
    if (total > 0) {
        int bw = btn_w(total);
        int by = TASKBAR_Y + BTN_MARGIN;
        int bh = TASKBAR_H_PX - 2 * BTN_MARGIN;

        for (int i = 0; i < total; i++) {
            int id = wm_get_z_id(i);
            int bx = btn_x(i, total);

            /* Focused window (paling atas z-order) = warna terang */
            int focused   = (i == total - 1);
            int minimized = wm_is_minimized(id);
            uint8_t bg = focused ? GFX_BLUE : (minimized ? GFX_DGRAY : GFX_LGRAY);
            uint8_t fg = focused ? GFX_WHITE : GFX_BLACK;

            fill_rect(bx, by, bw, bh, bg);
            /* Border 1px: sunken style untuk window yang diminimalkan */
            uint8_t b_hi = minimized ? GFX_DGRAY : GFX_WHITE;
            uint8_t b_lo = minimized ? GFX_WHITE : GFX_DGRAY;
            fill_rect(bx, by, bw, 1, b_hi);
            fill_rect(bx, by, 1, bh, b_hi);
            fill_rect(bx + bw - 1, by, 1, bh, b_lo);
            fill_rect(bx, by + bh - 1, bw, 1, b_lo);

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

    /* Quick-launch area */
    if (mx < LAUNCH_W) {
        int idx = (mx - BTN_MARGIN) / LAUNCH_BTN_W;
        if (idx >= 0 && idx < LAUNCH_COUNT)
            kernel_exec(launch_names[idx]);
        return;
    }

    /* Window switcher */
    int total = wm_get_z_count();
    if (total == 0) return;

    int bw = btn_w(total);
    for (int i = 0; i < total; i++) {
        int bx = btn_x(i, total);
        if (mx >= bx && mx < bx + bw) {
            int wid = wm_get_z_id(i);
            if (wm_is_minimized(wid))
                wm_restore_by_id(wid);
            else
                wm_raise_by_id(wid);
            return;
        }
    }
}
