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
#include "taskbar.h"
#include "graphics.h"
#include "mouse.h"
#include <stdint.h>

/* ============================================================
 * Data internal
 * ============================================================ */

/* Satu entri backing store teks — disimpan tiap kali wm_draw_text dipanggil */
typedef struct {
    int     x, y;
    char    s[WIN_TEXTBUF_STR];
    uint8_t fg, bg;
} WinText;

typedef struct {
    int x, y, w, h;
    char label[24];
    int alive;
} WinBtn;

typedef struct {
    int x, y, w, h;
    char title[32];
    uint8_t content_bg;
    int alive;
    /* antrian event ring-buffer (int agar bisa muat event gabungan) */
    int ev_q[WIN_EVQ_SIZE];
    int ev_head, ev_tail;
    /* backing store teks: di-replay ulang setiap kali window di-redraw */
    WinText text_buf[WIN_TEXTBUF];
    int     text_count;
    /* antrian keyboard per-window */
    char key_q[WIN_KEYQ_SIZE];
    int  key_head, key_tail;
    /* tombol (button widget) */
    WinBtn buttons[WIN_BTN_MAX];
    int    btn_count;
    /* koordinat klik konten terakhir (piksel relatif area konten) */
    int    last_click_x, last_click_y;
} WinSlot;

static WinSlot windows[MAX_WINDOWS];

/* z_order[0]=paling bawah, z_order[z_count-1]=paling atas */
static int z_order[MAX_WINDOWS];
static int z_count = 0;

/* state drag */
static int drag_id = -1;
static int drag_ox, drag_oy;  /* offset mouse relatif terhadap sudut kiri atas window */

/* state mouse global (diperbarui setiap wm_mouse_event) */
static int   g_mouse_x   = 0;
static int   g_mouse_y   = 0;
static uint8_t g_mouse_btn = 0;

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
static void ev_push(int id, int ev) {
    WinSlot *w = &windows[id];
    int next = (w->ev_tail + 1) % WIN_EVQ_SIZE;
    if (next == w->ev_head) return;  /* penuh, buang event */
    w->ev_q[w->ev_tail] = ev;
    w->ev_tail = next;
}

static int ev_pop(int id) {
    WinSlot *w = &windows[id];
    if (w->ev_head == w->ev_tail) return WIN_EVENT_NONE;
    int ev = w->ev_q[w->ev_head];
    w->ev_head = (w->ev_head + 1) % WIN_EVQ_SIZE;
    return ev;
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
    if (ca_h <= 0) return;
    fill_rect(ca_x, ca_y, ca_w, ca_h, w->content_bg);

    /* Replay backing store teks */
    for (int t = 0; t < w->text_count; t++) {
        WinText *te = &w->text_buf[t];
        int sx = ca_x + te->x;
        int sy = ca_y + te->y;
        int mpx = (ca_x + ca_w) - sx;
        if (mpx > 0 && sy >= ca_y && sy + 8 <= ca_y + ca_h)
            wm_drawstr(sx, sy, te->s, te->fg, te->bg, mpx);
    }

    /* Render tombol (button widget) */
    for (int b = 0; b < w->btn_count; b++) {
        WinBtn *btn = &w->buttons[b];
        if (!btn->alive) continue;
        int bx = ca_x + btn->x;
        int by = ca_y + btn->y;
        if (bx >= ca_x + ca_w || by >= ca_y + ca_h) continue;
        fill_rect(bx, by, btn->w, btn->h, GFX_LGRAY);
        /* 3D border: putih di atas/kiri, gelap di bawah/kanan */
        fill_rect(bx,              by,              btn->w, 1,       GFX_WHITE);
        fill_rect(bx,              by,              1,      btn->h,  GFX_WHITE);
        fill_rect(bx,              by + btn->h - 1, btn->w, 1,       GFX_DGRAY);
        fill_rect(bx + btn->w - 1, by,              1,      btn->h,  GFX_DGRAY);
        /* label terpusat */
        int llen = 0;
        while (btn->label[llen]) llen++;
        int tx = bx + (btn->w - llen * 8) / 2;
        int ty = by + (btn->h - 8) / 2;
        if (tx < bx) tx = bx;
        if (ty < by) ty = by;
        wm_drawstr(tx, ty, btn->label, GFX_BLACK, GFX_LGRAY, bx + btn->w - tx);
    }
}

static void wm_redraw_all(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, WM_DESKTOP_BG);
    for (int i = 0; i < z_count; i++)
        wm_draw_window(z_order[i]);
    taskbar_draw();  /* gambar taskbar di atas segalanya */
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
        windows[i].alive      = 0;
        windows[i].ev_head    = 0;
        windows[i].ev_tail    = 0;
        windows[i].text_count = 0;
        windows[i].key_head   = 0;
        windows[i].key_tail   = 0;
        windows[i].btn_count  = 0;
        windows[i].last_click_x = 0;
        windows[i].last_click_y = 0;
        for (int b = 0; b < WIN_BTN_MAX; b++)
            windows[i].buttons[b].alive = 0;
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
    win->text_count = 0;
    win->key_head   = 0;
    win->key_tail   = 0;
    win->btn_count  = 0;
    win->last_click_x = 0;
    win->last_click_y = 0;
    for (int b = 0; b < WIN_BTN_MAX; b++)
        win->buttons[b].alive = 0;
    wm_strncpy(win->title, title, 32);

    /* tambah ke paling atas z-order */
    z_order[z_count++] = id;

    if (first_window) {
        /* Pertama kali: cat ulang seluruh layar jadi desktop */
        fill_rect(0, 0, SCREEN_W, SCREEN_H, WM_DESKTOP_BG);
    }
    /* Selalu redraw penuh: taskbar harus update untuk tampilkan tombol baru */
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

    /* Simpan ke backing store:
     * jika sudah ada entri di (px,py), overwrite; jika tidak, append */
    int slot = -1;
    for (int t = 0; t < w->text_count; t++) {
        if (w->text_buf[t].x == px && w->text_buf[t].y == py) {
            slot = t; break;
        }
    }
    if (slot < 0 && w->text_count < WIN_TEXTBUF)
        slot = w->text_count++;
    if (slot >= 0) {
        WinText *te = &w->text_buf[slot];
        te->x = px; te->y = py; te->fg = fg; te->bg = bg;
        int k = 0;
        while (k < WIN_TEXTBUF_STR - 1 && s[k]) { te->s[k] = s[k]; k++; }
        te->s[k] = '\0';
    }

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
    w->content_bg  = bg;
    w->text_count  = 0;  /* hapus seluruh backing store teks */
    /* Redraw window penuh agar tombol (buttons) tetap terlihat setelah clear */
    wm_draw_window(id);
}

int wm_poll_event(int id) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return WIN_EVENT_NONE;
    /* antrian event biasa dulu (close, click, btn) */
    int ev = ev_pop(id);
    if (ev != WIN_EVENT_NONE) return ev;
    /* kemudian antrian keyboard */
    WinSlot *w = &windows[id];
    if (w->key_head != w->key_tail) {
        unsigned char c = (unsigned char)w->key_q[w->key_head];
        w->key_head = (w->key_head + 1) % WIN_KEYQ_SIZE;
        return WIN_EVENT_KEY | (c << 8);
    }
    return WIN_EVENT_NONE;
}

/* ============================================================
 * Integrasi Mouse — dipanggil dari mouse_handler()
 * ============================================================ */
void wm_mouse_event(int nx, int ny, uint8_t new_btn, uint8_t old_btn) {
    uint8_t pressed  = new_btn & (~old_btn);   /* tombol baru ditekan */
    int redraw = 0;

    /* simpan state mouse global */
    g_mouse_x   = nx;
    g_mouse_y   = ny;
    g_mouse_btn = new_btn;

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
            /* jangan biarkan window melewati area taskbar */
            if (ny_w + w->h > SCREEN_H - TASKBAR_H_PX) ny_w = SCREEN_H - TASKBAR_H_PX - w->h;
            if (ny_w < 0) ny_w = 0;  /* fallback jika window lebih tinggi dari layar-taskbar */
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
        /* Cek taskbar lebih dulu */
        if (ny >= SCREEN_H - TASKBAR_H_PX) {
            taskbar_click(nx, ny);
        } else {
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
                    WinSlot *ww = &windows[id];
                    drag_id = id;
                    drag_ox = nx - ww->x;
                    drag_oy = ny - ww->y;
                } else {
                    /* klik area konten: cek tombol dulu */
                    WinSlot *ww = &windows[id];
                    int ca_x2 = ww->x + BORDER_W;
                    int ca_y2 = ww->y + BORDER_W + TITLEBAR_H;
                    int hit_btn = -1;
                    for (int b = 0; b < ww->btn_count; b++) {
                        WinBtn *btn = &ww->buttons[b];
                        if (!btn->alive) continue;
                        int bx = ca_x2 + btn->x;
                        int by = ca_y2 + btn->y;
                        if (nx >= bx && nx < bx + btn->w &&
                            ny >= by && ny < by + btn->h) {
                            hit_btn = b;
                            break;
                        }
                    }
                    if (hit_btn >= 0)
                        ev_push(id, WIN_EVENT_BTN | (hit_btn << 8));
                    else {
                        /* Simpan koordinat klik relatif area konten */
                        ww->last_click_x = nx - ca_x2;
                        ww->last_click_y = ny - ca_y2;
                        ev_push(id, WIN_EVENT_CLICK);
                    }
                }
            }
        }
    }

    if (redraw) wm_redraw_all();
}

/* ============================================================
 * Fungsi baru: keyboard routing, aksesor, button add
 * ============================================================ */

int wm_has_focus(void) {
    return z_count > 0;
}

void wm_key_event(char c) {
    if (z_count == 0) return;
    int id = z_order[z_count - 1];   /* window paling atas */
    WinSlot *w = &windows[id];
    int next = (w->key_tail + 1) % WIN_KEYQ_SIZE;
    if (next == w->key_head) return;  /* penuh */
    w->key_q[w->key_tail] = c;
    w->key_tail = next;
}

int wm_get_z_count(void) {
    return z_count;
}

int wm_get_z_id(int idx) {
    if (idx < 0 || idx >= z_count) return -1;
    return z_order[idx];
}

const char *wm_get_title(int id) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return "";
    return windows[id].title;
}

void wm_raise_by_id(int id) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return;
    z_raise(id);
    wm_redraw_all();
}

void wm_get_click_pos(int id, int *out_x, int *out_y) {
    if (id < 0 || id >= MAX_WINDOWS || !out_x || !out_y) return;
    *out_x = windows[id].last_click_x;
    *out_y = windows[id].last_click_y;
}

void wm_draw_pixel(int id, int cx, int cy, uint8_t color) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return;
    WinSlot *w = &windows[id];
    int ca_x = w->x + BORDER_W;
    int ca_y = w->y + BORDER_W + TITLEBAR_H;
    int ca_w = w->w - 2 * BORDER_W;
    int ca_h = w->h - 2 * BORDER_W - TITLEBAR_H;
    /* Clamp ke area konten */
    if (cx < 0 || cy < 0 || cx >= ca_w || cy >= ca_h) return;
    int sx = ca_x + cx, sy = ca_y + cy;
    draw_pixel(sx, sy, color);
    cursor_update_pixel(sx, sy, color);  /* perbaiki cursor_bg agar cursor_erase tidak hapus pixel paint */
}

/* Kembalikan posisi mouse relatif area konten window id.
 * out_x, out_y: koordinat relatif (bisa negatif jika di luar area konten).
 * Return: button state (bit0=kiri, bit1=kanan, bit2=tengah). */
int wm_mouse_rel(int id, int *out_x, int *out_y) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive || !out_x || !out_y)
        return 0;
    WinSlot *w = &windows[id];
    int ca_x = w->x + BORDER_W;
    int ca_y = w->y + BORDER_W + TITLEBAR_H;
    *out_x = g_mouse_x - ca_x;
    *out_y = g_mouse_y - ca_y;
    return (int)g_mouse_btn;
}

void wm_fill_rect(int id, int cx, int cy, int rw, int rh, uint8_t color) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return;
    WinSlot *w = &windows[id];
    int ca_x = w->x + BORDER_W;
    int ca_y = w->y + BORDER_W + TITLEBAR_H;
    int ca_w = w->w - 2 * BORDER_W;
    int ca_h = w->h - 2 * BORDER_W - TITLEBAR_H;
    /* Clip ke area konten */
    if (cx < 0) { rw += cx; cx = 0; }
    if (cy < 0) { rh += cy; cy = 0; }
    if (cx + rw > ca_w) rw = ca_w - cx;
    if (cy + rh > ca_h) rh = ca_h - cy;
    if (rw <= 0 || rh <= 0) return;
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++)
            draw_pixel(ca_x + cx + x, ca_y + cy + y, color);
    /* Perbarui cursor_bg untuk area yang di-overwrite */
    cursor_update_region(ca_x + cx, ca_y + cy, rw, rh, color);
}

int wm_btn_add(int id, int x, int y, int w, int h, const char *label) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return -1;
    WinSlot *ws = &windows[id];
    if (ws->btn_count >= WIN_BTN_MAX) return -1;
    int idx = ws->btn_count++;
    WinBtn *btn = &ws->buttons[idx];
    btn->x = x; btn->y = y; btn->w = w; btn->h = h;
    btn->alive = 1;
    int k = 0;
    if (label)
        while (k < 23 && label[k]) { btn->label[k] = label[k]; k++; }
    btn->label[k] = '\0';
    wm_redraw_all();   /* gambar ulang agar tombol langsung muncul */
    return idx;
}
