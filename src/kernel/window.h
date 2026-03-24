#ifndef WINDOW_H
#define WINDOW_H
#include <stdint.h>

/* ============================================================
 * Konstanta Window Manager
 * ============================================================ */
#define MAX_WINDOWS     16   /* maksimum window yang bisa hidup bersamaan */
#define TITLEBAR_H      18   /* tinggi title bar dalam piksel */
#define CLOSE_BTN_W     18   /* lebar tombol tutup (X) */
#define BORDER_W         1   /* tebal border window */
#define WIN_EVQ_SIZE    16   /* kapasitas antrian event per window */
#define WIN_TEXTBUF     32   /* maks baris teks yang disimpan per window (backing store) */
#define WIN_TEXTBUF_STR 64   /* panjang maks string per entri backing store */
#define WIN_BTN_MAX      8   /* maks tombol per window */
#define WIN_KEYQ_SIZE   32   /* kapasitas antrian karakter keyboard per window */

#define WM_DESKTOP_BG    8   /* GFX_DGRAY — warna latar desktop */

/* ---- Event Types ---- */
#define WIN_EVENT_NONE   0   /* tidak ada event */
#define WIN_EVENT_CLOSE  1   /* pengguna klik tombol X */
#define WIN_EVENT_CLICK  2   /* klik kiri di area konten (bukan tombol) */
#define WIN_EVENT_KEY    3   /* ada input keyboard: (char << 8) | WIN_EVENT_KEY */
#define WIN_EVENT_BTN    4   /* tombol diklik: (btn_idx << 8) | WIN_EVENT_BTN */

/* Macro helper untuk decode event */
#define WIN_EV_TYPE(ev)    ((ev) & 0xFF)
#define WIN_EV_CHAR(ev)    ((char)(((ev) >> 8) & 0xFF))
#define WIN_EV_BTN(ev)     (((ev) >> 8) & 0xFF)

/* ============================================================
 * Struct argumen syscall
 * ============================================================ */

/* Argumen SYS_WIN_CREATE: buat window baru */
typedef struct {
    int x, y, w, h;
    const char *title;
} WinCreateArgs;

/* Argumen SYS_WIN_DRAW: gambar teks di area konten window */
typedef struct {
    int       id;         /* id window tujuan       */
    int       x, y;       /* offset piksel di konten */
    const char *s;        /* string null-terminated  */
    uint8_t   fg, bg;     /* warna teks / latar       */
} WinDrawArgs;

/* Argumen SYS_WIN_BTN_ADD: tambah tombol ke window */
typedef struct {
    int id;
    int x, y, w, h;
    const char *label;
} WinBtnArgs;

/* ============================================================
 * Fungsi publik Window Manager
 * ============================================================ */
void wm_init(void);

/* Dipanggil oleh mouse_handler() setelah update posisi */
void wm_mouse_event(int nx, int ny, uint8_t new_btn, uint8_t old_btn);

/* Dipanggil dari keyboard polling loop di kernel.c */
void wm_key_event(char c);
int  wm_has_focus(void);   /* return 1 jika ada window aktif */

/* Syscall interface (dipanggil dari syscall.c) */
int  wm_create(int x, int y, int w, int h, const char *title);
void wm_destroy(int id);
void wm_draw_text(int id, int px, int py, const char *s, uint8_t fg, uint8_t bg);
void wm_clear_content(int id, uint8_t bg);
int  wm_poll_event(int id);   /* return WIN_EVENT_* dengan encoding di byte atas */
int  wm_btn_add(int id, int x, int y, int w, int h, const char *label);

/* Accessor untuk taskbar.c */
int         wm_get_z_count(void);
int         wm_get_z_id(int idx);
const char *wm_get_title(int id);
void        wm_raise_by_id(int id);

/* Koordinat klik terakhir di area konten window (piksel relatif konten) */
void wm_get_click_pos(int id, int *out_x, int *out_y);

/* Gambar piksel di koordinat konten window (mengikuti posisi window) */
void wm_draw_pixel(int id, int cx, int cy, uint8_t color);
void wm_fill_rect(int id, int cx, int cy, int rw, int rh, uint8_t color);
int  wm_mouse_rel(int id, int *out_x, int *out_y);

#endif /* WINDOW_H */

