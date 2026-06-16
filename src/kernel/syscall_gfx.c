#include "syscall.h"
#include "syscall_internal.h"
#include "graphics.h"
#include "mouse.h"
#include "window.h"

extern void clear_screen();         // dari kernel.c

uint64_t syscall_dispatch_gfx(uint64_t eax, uint64_t ebx, uint64_t edx, int *handled) {
    *handled = 1;

    // SYS_DRAW_PIXEL(22): gambar satu piksel di framebuffer
    // ebx = ptr ke struct { int x, y; uint32_t color; }
    if (eax == SYS_DRAW_PIXEL) {
        typedef struct { int x, y; uint32_t color; } DrawPixelArgs;
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        DrawPixelArgs *a = (DrawPixelArgs*)ebx;
        draw_pixel(a->x, a->y, a->color);
        return 0;
    }
    // SYS_FILL_SCREEN(23): ebx=color 32bpp — isi seluruh layar
    if (eax == SYS_FILL_SCREEN) {
        fill_screen((uint32_t)ebx);
        return 0;
    }
    // SYS_FILL_RECT(24): ebx=pointer ke GfxRect
    if (eax == SYS_FILL_RECT) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        GfxRect *r = (GfxRect*)ebx;
        fill_rect(r->x, r->y, r->w, r->h, r->color);
        return 0;
    }
    // SYS_DRAW_LINE(25): ebx=pointer ke GfxLine — gambar garis Bresenham
    if (eax == SYS_DRAW_LINE) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        GfxLine *l = (GfxLine*)ebx;
        draw_line(l->x1, l->y1, l->x2, l->y2, l->color);
        return 0;
    }
    // SYS_CLR_SCREEN(26): bersihkan layar + reset kursor ke (0,0)
    if (eax == SYS_CLR_SCREEN) {
        clear_screen();
        return 0;
    }

    // SYS_DRAW_CHAR(31): gambar satu karakter 8x8 di framebuffer
    // ebx = ptr ke struct { int x, y; char c; char _pad[3]; uint32_t fg, bg; }
    if (eax == SYS_DRAW_CHAR) {
        typedef struct { int x, y; char c; char _pad[3]; uint32_t fg, bg; } DrawCharArgs;
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        DrawCharArgs *a = (DrawCharArgs*)ebx;
        draw_char_gfx(a->x, a->y, a->c, a->fg, a->bg);
        return 0;
    }

    // SYS_DRAW_STR(32): gambar string di framebuffer
    // ebx = ptr ke GfxStr { int x, y; const char *s; uint32_t fg, bg; }
    if (eax == SYS_DRAW_STR) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        GfxStr *gs = (GfxStr*)ebx;
        if (!is_user_ptr((uint64_t)gs->s)) return (uint64_t)-1;
        draw_string_gfx(gs->x, gs->y, gs->s, gs->fg, gs->bg);
        return 0;
    }

    // SYS_MOUSE_GET(33): baca state mouse ke struct MouseState
    // ebx = ptr ke MouseState { int x, y; uint8_t buttons; }
    if (eax == SYS_MOUSE_GET) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        mouse_get_state((MouseState*)ebx);
        return 0;
    }

    // SYS_WIN_CREATE(34): buat window baru
    // ebx = ptr ke WinCreateArgs { int x, y, w, h; const char *title; }
    // return: window id (0..MAX_WINDOWS-1) atau -1 jika gagal
    if (eax == SYS_WIN_CREATE) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        WinCreateArgs *a = (WinCreateArgs*)ebx;
        if (a->title && !is_user_ptr((uint64_t)a->title)) return (uint64_t)-1;
        return (uint64_t)wm_create(a->x, a->y, a->w, a->h, a->title);
    }
    // SYS_WIN_DESTROY(35): tutup window, bebaskan slot
    // ebx = window id
    if (eax == SYS_WIN_DESTROY) {
        wm_destroy((int)ebx);
        return 0;
    }
    // SYS_WIN_DRAW(36): gambar teks di area konten window
    // ebx = ptr ke WinDrawArgs { int id, x, y; const char *s; uint32_t fg, bg; }
    if (eax == SYS_WIN_DRAW) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        WinDrawArgs *d = (WinDrawArgs*)ebx;
        if (!is_user_ptr((uint64_t)d->s)) return (uint64_t)-1;
        wm_draw_text(d->id, d->x, d->y, d->s, d->fg, d->bg);
        return 0;
    }
    // SYS_WIN_CLEAR(37): bersihkan area konten window dengan warna bg 32bpp
    // ebx = window id, edx = warna background (uint32_t)
    if (eax == SYS_WIN_CLEAR) {
        wm_clear_content((int)ebx, (uint32_t)edx);
        return 0;
    }
    // SYS_WIN_EVENT(38): ambil event dari antrian window
    // ebx = window id; return WIN_EVENT_* (encode char/btn di byte atas)
    if (eax == SYS_WIN_EVENT) {
        return (uint32_t)wm_poll_event((int)ebx);
    }

    // SYS_WIN_BTN_ADD(39): tambah tombol ke window
    // ebx = ptr WinBtnArgs; return btn_idx atau -1
    if (eax == SYS_WIN_BTN_ADD) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        WinBtnArgs *a = (WinBtnArgs*)ebx;
        if (a->label && !is_user_ptr((uint64_t)a->label)) return (uint64_t)-1;
        return (uint64_t)wm_btn_add(a->id, a->x, a->y, a->w, a->h, a->label);
    }

    // SYS_WIN_CLICK_POS(40): ambil koordinat klik konten terakhir
    // ebx = window id; edx = ptr int[2] (output x, y relatif konten)
    if (eax == SYS_WIN_CLICK_POS) {
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        int *pos = (int*)edx;
        wm_get_click_pos((int)ebx, &pos[0], &pos[1]);
        return 0;
    }

    // SYS_WIN_DRAW_PIXEL(41): gambar piksel di koordinat area konten window
    // ebx = window id; edx = ptr ke struct { int cx, cy; uint32_t color; }
    if (eax == SYS_WIN_DRAW_PIXEL) {
        typedef struct { int cx, cy; uint32_t color; } WinPixelArgs;
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        WinPixelArgs *a = (WinPixelArgs*)edx;
        wm_draw_pixel((int)ebx, a->cx, a->cy, a->color);
        return 0;
    }

    // SYS_WIN_FILL_RECT(42): isi persegi di area konten window
    // ebx = window id; edx = ptr ke struct { short x,y,w,h; uint32_t color; }
    if (eax == SYS_WIN_FILL_RECT) {
        typedef struct { short x, y, w, h; uint32_t color; } WinFillArgs;
        if (!is_user_ptr(edx)) return 0;
        WinFillArgs *a = (WinFillArgs*)edx;
        wm_fill_rect((int)ebx, a->x, a->y, a->w, a->h, a->color);
        return 0;
    }

    // SYS_WIN_MOUSE_REL(43): posisi mouse relatif area konten window
    // ebx = window id; edx = ptr int[3] → [rel_x, rel_y, btn_state]
    if (eax == SYS_WIN_MOUSE_REL) {
        if (!is_user_ptr(edx)) return 0;
        int *out = (int*)edx;
        out[2] = wm_mouse_rel((int)ebx, &out[0], &out[1]);
        return 0;
    }

    // SYS_WIN_MINIMIZE(44): minimize window
    if (eax == SYS_WIN_MINIMIZE) {
        wm_minimize_by_id((int)ebx);
        return 0;
    }

    // SYS_WIN_RESTORE(45): restore window dari minimized
    if (eax == SYS_WIN_RESTORE) {
        wm_restore_by_id((int)ebx);
        return 0;
    }

    // SYS_GFX_FLIP(105): flush back buffer ke hardware framebuffer
    if (eax == SYS_GFX_FLIP) {
        gfx_flip();
        return 0;
    }

    *handled = 0;
    return 0;
}
