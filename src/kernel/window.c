/*
 * window.c — Window Manager sederhana
 *
 * Mendukung hingga MAX_WINDOWS window secara bersamaan.
 * Rendering langsung ke framebuffer VBE 640x480 @8bpp via graphics.c.
 * Interaksi mouse (drag, klik, tutup) diproses lewat wm_mouse_event()
 * yang dipanggil dari mouse_handler() di mouse.c.
 *
 * Layout window:
 *   ┌─[Border 1px]──────────────────────[X]─┐  ← TITLEBAR_H piksel
 *   │  [Area Konten]                         │
 *   └────────────────────────────────────────┘
 */
#include "window.h"
#include "graphics.h"
#include <stdint.h>

/* ============================================================
 * Data internal
 * ============================================================ */
typedef struct {
    int x, y, w, h;
    char title[32];
    uint8_t content_bg;
    int alive;
    /* antrian event ring-buffer */
    uint8_t ev_q[WIN_EVQ_SIZE];
    int ev_head, ev_tail;
} WinSlot;

static WinSlot windows[MAX_WINDOWS];

/* z_order[0]=paling bawah, z_order[z_count-1]=paling atas */
static int z_order[MAX_WINDOWS];
static int z_count = 0;

/* state drag */
static int drag_id = -1;
static int drag_ox, drag_oy;  /* offset mouse relatif terhadap sudut kiri atas window */

/* ============================================================
 * Helper: string
 * ============================================================ */
static void wm_strncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ============================================================
 * Helper: event queue
 * ============================================================ */
static void ev_push(int id, uint8_t type) {
    WinSlot *w = &windows[id];
    int next = (w->ev_tail + 1) % WIN_EVQ_SIZE;
    if (next == w->ev_head) return;  /* penuh, buang event */
    w->ev_q[w->ev_tail] = type;
    w->ev_tail = next;
}

static int ev_pop(int id) {
    WinSlot *w = &windows[id];
    if (w->ev_head == w->ev_tail) return WIN_EVENT_NONE;
    int type = w->ev_q[w->ev_head];
    w->ev_head = (w->ev_head + 1) % WIN_EVQ_SIZE;
    return type;
}

/* ============================================================
 * Helper: z-order
 * ============================================================ */
static int z_find(int id) {
    for (int i = 0; i < z_count; i++)
        if (z_order[i] == id) return i;
    return -1;
}

/* Pindahkan window ke paling atas z-order */
static void z_raise(int id) {
    int pos = z_find(id);
    if (pos < 0) return;
    for (int i = pos; i < z_count - 1; i++)
        z_order[i] = z_order[i + 1];
    z_order[z_count - 1] = id;
}

/* ============================================================
 * Rendering
 * ============================================================ */

/* Gambar string dengan batas piksel max_px (agar tidak melampaui area) */
static void wm_drawstr(int x, int y, const char *s, uint8_t fg, uint8_t bg, int max_px) {
    int cx = x;
    for (int i = 0; s[i] && (cx + 8 <= x + max_px); i++, cx += 8)
        draw_char_gfx(cx, y, s[i], fg, bg);
}

static void wm_draw_window(int id) {
    WinSlot *w = &windows[id];
    if (!w->alive) return;

    int focused = (z_count > 0 && z_order[z_count - 1] == id);
    uint8_t tb_color = focused ? GFX_BLUE : GFX_DGRAY;

    /* --- Border (1px, abu-abu terang) --- */
    fill_rect(w->x,                      w->y,                       w->w, BORDER_W, GFX_LGRAY);
    fill_rect(w->x,                      w->y + w->h - BORDER_W,     w->w, BORDER_W, GFX_LGRAY);
    fill_rect(w->x,                      w->y,                       BORDER_W, w->h, GFX_LGRAY);
    fill_rect(w->x + w->w - BORDER_W,    w->y,                       BORDER_W, w->h, GFX_LGRAY);

    /* --- Title bar --- */
    int tb_x = w->x + BORDER_W;
    int tb_y = w->y + BORDER_W;
    int tb_w = w->w - 2 * BORDER_W;
    fill_rect(tb_x, tb_y, tb_w, TITLEBAR_H, tb_color);

    /* Teks judul (maks sampai tombol close, padding 5px dari kiri) */
    int title_max_px = tb_w - CLOSE_BTN_W - 6;
    if (title_max_px > 0)
        wm_drawstr(tb_x + 5, tb_y + (TITLEBAR_H - 8) / 2,
                   w->title, GFX_WHITE, tb_color, title_max_px);

    /* --- Tombol Close (X, merah) --- */
    int cb_x = w->x + w->w - BORDER_W - CLOSE_BTN_W;
    fill_rect(cb_x, tb_y, CLOSE_BTN_W, TITLEBAR_H, GFX_RED);
    draw_char_gfx(cb_x + (CLOSE_BTN_W - 8) / 2,
                  tb_y + (TITLEBAR_H - 8) / 2, 'X', GFX_WHITE, GFX_RED);

    /* --- Area Konten --- */
    int ca_x = w->x + BORDER_W;
    int ca_y = w->y + BORDER_W + TITLEBAR_H;
    int ca_w = w->w - 2 * BORDER_W;
    int ca_h = w->h - 2 * BORDER_W - TITLEBAR_H;
    if (ca_h > 0)
        fill_rect(ca_x, ca_y, ca_w, ca_h, w->content_bg);
}

static void wm_redraw_all(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, WM_DESKTOP_BG);
    for (int i = 0; i < z_count; i++)
        wm_draw_window(z_order[i]);
}

/* ============================================================
 * Hit testing
 * ============================================================ */

/* Temukan window paling atas di koordinat (mx, my), return id atau -1 */
static int hit_window(int mx, int my) {
    for (int i = z_count - 1; i >= 0; i--) {
        int id = z_order[i];
        WinSlot *w = &windows[id];
        if (!w->alive) continue;
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h)
            return id;
    }
    return -1;
}

/* Apakah (mx,my) di tombol close window id? */
static int hit_close(int id, int mx, int my) {
    WinSlot *w = &windows[id];
    int cb_x = w->x + w->w - BORDER_W - CLOSE_BTN_W;
    int cb_y = w->y + BORDER_W;
    return (mx >= cb_x && mx < cb_x + CLOSE_BTN_W &&
            my >= cb_y && my < cb_y + TITLEBAR_H);
}

/* Apakah (mx,my) di title bar (bukan tombol close) window id? */
static int hit_titlebar(int id, int mx, int my) {
    WinSlot *w = &windows[id];
    int tb_y = w->y + BORDER_W;
    int tb_end_x = w->x + w->w - BORDER_W - CLOSE_BTN_W;
    return (mx >= w->x + BORDER_W && mx < tb_end_x &&
            my >= tb_y && my < tb_y + TITLEBAR_H);
}

/* ============================================================
 * Fungsi publik
 * ============================================================ */

void wm_init(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].alive    = 0;
        windows[i].ev_head  = 0;
        windows[i].ev_tail  = 0;
    }
    z_count  = 0;
    drag_id  = -1;
    /* Layar TIDAK dibersihkan di sini — shell sudah menggambar prompt-nya.
     * Pembersihan dilakukan oleh program GUI yang berjalan (e.g. gui_demo). */
}

int wm_create(int x, int y, int w, int h, const char *title) {
    /* cari slot kosong */
    int id = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].alive) { id = i; break; }
    }
    if (id < 0) return -1;

    /* Bersihkan layar menjadi desktop hanya saat window PERTAMA dibuat */
    int first_window = (z_count == 0);

    /* ukuran minimum */
    int min_w = CLOSE_BTN_W + 2 * BORDER_W + 30;
    int min_h = TITLEBAR_H  + 2 * BORDER_W + 10;
    if (w < min_w) w = min_w;
    if (h < min_h) h = min_h;

    WinSlot *win    = &windows[id];
    win->x          = x;
    win->y          = y;
    win->w          = w;
    win->h          = h;
    win->content_bg = GFX_BLACK;
    win->alive      = 1;
    win->ev_head    = 0;
    win->ev_tail    = 0;
    wm_strncpy(win->title, title, 32);

    /* tambah ke paling atas z-order */
    z_order[z_count++] = id;

    if (first_window) {
        /* Pertama kali: cat ulang seluruh layar jadi desktop */
        fill_rect(0, 0, SCREEN_W, SCREEN_H, WM_DESKTOP_BG);
    }

    wm_redraw_all();
    return id;
}

void wm_destroy(int id) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return;
    windows[id].alive = 0;

    int pos = z_find(id);
    if (pos >= 0) {
        for (int i = pos; i < z_count - 1; i++)
            z_order[i] = z_order[i + 1];
        z_count--;
    }
    if (drag_id == id) drag_id = -1;

    wm_redraw_all();
}

void wm_draw_text(int id, int px, int py, const char *s, uint8_t fg, uint8_t bg) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive || !s) return;
    WinSlot *w  = &windows[id];

    int ca_x = w->x + BORDER_W;
    int ca_y = w->y + BORDER_W + TITLEBAR_H;
    int ca_w = w->w - 2 * BORDER_W;
    int ca_h = w->h - 2 * BORDER_W - TITLEBAR_H;

    int sx = ca_x + px;
    int sy = ca_y + py;
    int max_px = (ca_x + ca_w) - sx;

    if (max_px <= 0 || sy < ca_y || sy + 8 > ca_y + ca_h) return;
    wm_drawstr(sx, sy, s, fg, bg, max_px);
}

void wm_clear_content(int id, uint8_t bg) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return;
    WinSlot *w = &windows[id];
    w->content_bg = bg;

    int ca_x = w->x + BORDER_W;
    int ca_y = w->y + BORDER_W + TITLEBAR_H;
    int ca_w = w->w - 2 * BORDER_W;
    int ca_h = w->h - 2 * BORDER_W - TITLEBAR_H;
    if (ca_h > 0) fill_rect(ca_x, ca_y, ca_w, ca_h, bg);
}

int wm_poll_event(int id) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return WIN_EVENT_NONE;
    return ev_pop(id);
}

/* ============================================================
 * Integrasi Mouse — dipanggil dari mouse_handler()
 * ============================================================ */
void wm_mouse_event(int nx, int ny, uint8_t new_btn, uint8_t old_btn) {
    uint8_t pressed  = new_btn & (~old_btn);   /* tombol baru ditekan */
    int redraw = 0;

    /* --- Proses drag aktif --- */
    if (drag_id >= 0) {
        if (new_btn & 0x01) {
            /* masih drag: pindahkan window */
            WinSlot *w = &windows[drag_id];
            int nx_w = nx - drag_ox;
            int ny_w = ny - drag_oy;
            /* clamp agar tidak keluar layar */
            if (nx_w < 0) nx_w = 0;
            if (ny_w < 0) ny_w = 0;
            if (nx_w + w->w > SCREEN_W) nx_w = SCREEN_W - w->w;
            if (ny_w + w->h > SCREEN_H) ny_w = SCREEN_H - w->h;
            if (nx_w != w->x || ny_w != w->y) {
                w->x = nx_w;
                w->y = ny_w;
                redraw = 1;
            }
        } else {
            /* tombol lepas — selesai drag */
            drag_id = -1;
        }
    }

    /* --- Proses klik baru --- */
    if (pressed & 0x01) {
        int id = hit_window(nx, ny);
        if (id >= 0) {
            /* naikkan ke atas jika belum di atas */
            if (z_count == 0 || z_order[z_count - 1] != id) {
                z_raise(id);
                redraw = 1;
            }

            if (hit_close(id, nx, ny)) {
                /* klik tombol tutup */
                ev_push(id, WIN_EVENT_CLOSE);
            } else if (hit_titlebar(id, nx, ny)) {
                /* mulai drag */
                WinSlot *w = &windows[id];
                drag_id = id;
                drag_ox = nx - w->x;
                drag_oy = ny - w->y;
            } else {
                /* klik area konten */
                ev_push(id, WIN_EVENT_CLICK);
            }
        }
    }

    if (redraw) wm_redraw_all();
}
