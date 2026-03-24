/*
 * paint.c — Aplikasi Paint sederhana
 *
 * Fitur:
 *  - Kanvas 16 warna (pixel per klik atau drag)
 *  - Palet warna di bawah kanvas (klik pilih warna aktif)
 *  - Tombol "Clear" dan "Close"
 *  - Ukuran brush 3x3 piksel agar lebih mudah menggambar
 */
#include "lib.h"

/* Dimensi window */
#define WIN_X  30
#define WIN_Y  30
#define WIN_W  500
#define WIN_H  360

/* Area konten tersedia: WIN_W-2 x WIN_H-2-TITLEBAR(18) = 498 x 340 */
#define CA_W  (WIN_W - 2)    /* lebar area konten */
#define CA_H  (WIN_H - 2 - 18)  /* tinggi area konten */

/* Kanvas: dari y=0 sampai CANVAS_H */
#define CANVAS_H  (CA_H - PALETTE_AREA_H - BTN_AREA_H)
#define PALETTE_AREA_H  18   /* baris palet warna */
#define BTN_AREA_H      22   /* baris tombol */
#define PALETTE_Y  CANVAS_H
#define BTN_Y      (CANVAS_H + PALETTE_AREA_H + 2)
#define BTN_H      16
#define BTN_CLEAR_X  4
#define BTN_CLEAR_W  52
#define BTN_CLOSE_X  62
#define BTN_CLOSE_W  52

/* Brush size (piksel, kotak NxN) */
#define BRUSH  3

/* 16 warna standar */
static const unsigned char palette[16] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15
};

static int win_id = -1;
static unsigned char cur_color = 4;   /* merah sebagai warna awal */

/* ------------------------------------------------------------------ */
/* Gambar strip palet warna di bawah kanvas                            */
/* ------------------------------------------------------------------ */
static void draw_palette(void) {
    int sw = CA_W / 16;   /* lebar tiap kotak warna */
    for (int i = 0; i < 16; i++) {
        int px = i * sw;
        /* Isi kotak dengan warna menggunakan win_fill_rect (1 syscall per kotak) */
        win_fill_rect(win_id, px, PALETTE_Y, sw - 1, PALETTE_AREA_H - 2, palette[i]);
        /* Tandai warna aktif dengan border putih */
        if (palette[i] == cur_color) {
            /* Garis atas & bawah */
            win_fill_rect(win_id, px, PALETTE_Y, sw - 1, 1, 15);
            win_fill_rect(win_id, px, PALETTE_Y + PALETTE_AREA_H - 3, sw - 1, 1, 15);
            /* Garis kiri & kanan */
            win_fill_rect(win_id, px, PALETTE_Y, 1, PALETTE_AREA_H - 2, 15);
            win_fill_rect(win_id, px + sw - 2, PALETTE_Y, 1, PALETTE_AREA_H - 2, 15);
        }
    }
}

/* Gambar piksel brush di (cx, cy) dengan warna aktif */
static void paint_at(int cx, int cy) {
    for (int dy = 0; dy < BRUSH; dy++)
        for (int dx = 0; dx < BRUSH; dx++)
            win_draw_pixel(win_id, cx - BRUSH/2 + dx, cy - BRUSH/2 + dy, cur_color);
}

/* Bersihkan kanvas (putih) */
static void clear_canvas(void) {
    /* Satu syscall untuk isi kanvas putih */
    win_fill_rect(win_id, 0, 0, CA_W, CANVAS_H, 15);
    /* Redraw palet setelah clear */
    draw_palette();
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */
void _start(void) {
    win_id = win_create(WIN_X, WIN_Y, WIN_W, WIN_H, "Paint");
    if (win_id < 0) exit();

    /* Tombol Clear dan Close */
    win_btn_add(win_id, BTN_CLEAR_X, BTN_Y, BTN_CLEAR_W, BTN_H, "Clear");
    win_btn_add(win_id, BTN_CLOSE_X, BTN_Y, BTN_CLOSE_W, BTN_H, "Close");

    /* Kanvas putih awal */
    clear_canvas();

    /* Event loop */
    while (1) {
        /* Cek drag DULU setiap iterasi (tombol ditahan, tidak perlu event) */
        int mx, my;
        int mbtn = win_mouse_rel(win_id, &mx, &my);
        if ((mbtn & 1) && mx >= 0 && mx < CA_W && my >= 0 && my < CANVAS_H) {
            paint_at(mx, my);
        }

        int ev = win_poll(win_id);
        if (ev == WIN_EVENT_NONE) {
            yield();
            continue;
        }

        int type = WIN_EV_TYPE(ev);

        if (type == WIN_EVENT_CLOSE) {
            break;

        } else if (type == WIN_EVENT_BTN) {
            int btn = WIN_EV_BTN(ev);
            if (btn == 0) {
                /* Clear */
                clear_canvas();
            } else if (btn == 1) {
                break;
            }

        } else if (type == WIN_EVENT_CLICK) {
            int cx, cy;
            win_click_pos(win_id, &cx, &cy);

            if (cy >= PALETTE_Y && cy < PALETTE_Y + PALETTE_AREA_H) {
                /* Klik area palet — ganti warna aktif */
                int sw = CA_W / 16;
                int idx = cx / sw;
                if (idx >= 0 && idx < 16) {
                    cur_color = palette[idx];
                    draw_palette();
                }
            } else if (cy < CANVAS_H) {
                paint_at(cx, cy);
            }
        }
    }

    win_destroy(win_id);
    exit();
}
