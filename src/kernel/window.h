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

#define WM_DESKTOP_BG    8   /* GFX_DGRAY — warna latar desktop */

/* ---- Event Types ---- */
#define WIN_EVENT_NONE   0   /* tidak ada event */
#define WIN_EVENT_CLOSE  1   /* pengguna klik tombol X */
#define WIN_EVENT_CLICK  2   /* klik kiri di area konten */

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

/* ============================================================
 * Fungsi publik Window Manager
 * ============================================================ */
void wm_init(void);

/* Dipanggil oleh mouse_handler() setelah update posisi */
void wm_mouse_event(int nx, int ny, uint8_t new_btn, uint8_t old_btn);

/* Syscall interface (dipanggil dari syscall.c) */
int  wm_create(int x, int y, int w, int h, const char *title);
void wm_destroy(int id);
void wm_draw_text(int id, int px, int py, const char *s, uint8_t fg, uint8_t bg);
void wm_clear_content(int id, uint8_t bg);
int  wm_poll_event(int id);   /* return WIN_EVENT_* */

#endif /* WINDOW_H */
