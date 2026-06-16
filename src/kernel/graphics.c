/* graphics.c — Primitif grafis 32bpp untuk VBE Linear Framebuffer */
#include "graphics.h"
#include "font8x8.h"
#include "memory.h"
#include <stdint.h>

/* -------------------------------------------------------------------
 * State global
 * ------------------------------------------------------------------- */

/* Alamat fisik LFB — diperbarui oleh graphics_set_fb() */
uint32_t gfx_lfb_addr = 0xE0000000U;

/* Back buffer (software) — static BSS array, 8MB di region 0x4000000+.
 * Tidak pakai malloc sehingga tidak bergantung pada ukuran heap (heap hanya 6MB). */
static uint32_t g_back_buf[SCREEN_W * SCREEN_H];
static uint32_t *back_buf = g_back_buf;

/* Pointer ke hardware framebuffer (diset oleh graphics_set_fb) */
static volatile uint32_t *hw_fb = 0;

/* Pointer tulis: awalnya ke hw_fb (console mode); switch ke back_buf saat GUI aktif via gfx_enable_double_buffer() */
static volatile uint32_t *fb = (volatile uint32_t *)0xE0000000U;

/* Dirty region — bounding box area yang sudah diubah sejak gfx_flip() terakhir.
 * x2/y2 eksklusif. has_dirty=0 berarti tidak ada yang perlu di-flip. */
static int dirty_x1, dirty_y1, dirty_x2, dirty_y2;
static int has_dirty = 0;

void gfx_mark_dirty(int x1, int y1, int x2, int y2) {
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int)SCREEN_W) x2 = (int)SCREEN_W;
    if (y2 > (int)SCREEN_H) y2 = (int)SCREEN_H;
    if (x2 <= x1 || y2 <= y1) return;
    if (!has_dirty) {
        dirty_x1 = x1; dirty_y1 = y1;
        dirty_x2 = x2; dirty_y2 = y2;
        has_dirty = 1;
    } else {
        if (x1 < dirty_x1) dirty_x1 = x1;
        if (y1 < dirty_y1) dirty_y1 = y1;
        if (x2 > dirty_x2) dirty_x2 = x2;
        if (y2 > dirty_y2) dirty_y2 = y2;
    }
}

/* -------------------------------------------------------------------
 * Inisialisasi
 * ------------------------------------------------------------------- */

/* Atur alamat LFB baru (hasil PCI scan vbe_find_lfb). */
void graphics_set_fb(uint32_t addr) {
    gfx_lfb_addr = addr;
    hw_fb        = (volatile uint32_t *)addr;
    fb           = hw_fb;   /* console mode: tulis langsung ke hardware */
}

/* Aktifkan double buffering saat memasuki GUI mode.
 * Harus dipanggil SEBELUM wm_init() — setelah ini semua draw ke back_buf, gfx_flip() ke hw. */
void gfx_enable_double_buffer(void) {
    fb = (volatile uint32_t *)back_buf;
}

/* Kembalikan pointer ke fb aktif (back_buf di GUI mode, hw_fb di console mode). */
volatile uint32_t *gfx_get_fb(void) { return fb; }

/* Salin back buffer ke hardware framebuffer — hanya baris dalam dirty region.
 * x1 dibulatkan ke bawah ke kelipatan 2 agar copy 64-bit tetap aligned. */
void gfx_flip(void) {
    if (!hw_fb || !back_buf || !has_dirty) return;
    int x1 = dirty_x1 & ~1;          /* round down ke kelipatan 2 pixel */
    int x2 = (dirty_x2 + 1) & ~1;    /* round up */
    if (x2 > (int)SCREEN_W) x2 = (int)SCREEN_W;
    int w2 = (x2 - x1) / 2;          /* lebar dalam unit 64-bit (2 pixel) */
    int y;
    for (y = dirty_y1; y < dirty_y2; y++) {
        uint64_t       *dst = (uint64_t *)(void *)(hw_fb    + (unsigned)(y * SCREEN_W + x1));
        const uint64_t *src = (const uint64_t *)(const void *)(back_buf + (unsigned)(y * SCREEN_W + x1));
        int i;
        for (i = 0; i < w2; i++) dst[i] = src[i];
    }
    has_dirty = 0;
}

/* Bersihkan layar ke warna hitam. */
void graphics_init() {
    fill_screen(GFX_BLACK);
}

/* -------------------------------------------------------------------
 * Primitif dasar
 * ------------------------------------------------------------------- */

/* Tulis satu piksel; koordinat di luar batas diabaikan (clipping). */
void draw_pixel(int x, int y, uint32_t color) {
    if ((unsigned int)x >= SCREEN_W || (unsigned int)y >= SCREEN_H) return;
    fb[y * SCREEN_W + x] = color;
    gfx_mark_dirty(x, y, x + 1, y + 1);
}

/* Isi seluruh layar dengan satu warna. */
void fill_screen(uint32_t color) {
    int i;
    for (i = 0; i < SCREEN_W * SCREEN_H; i++)
        fb[i] = color;
    gfx_mark_dirty(0, 0, (int)SCREEN_W, (int)SCREEN_H);
}

/* Isi persegi panjang (x, y, w, h) dengan satu warna.
 * Clip dilakukan sekali di luar loop — tidak ada bounds check per piksel. */
void fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (x < 0)              { w += x; x = 0; }
    if (y < 0)              { h += y; y = 0; }
    if (x + w > (int)SCREEN_W) w = (int)SCREEN_W - x;
    if (y + h > (int)SCREEN_H) h = (int)SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    int py;
    for (py = y; py < y + h; py++) {
        volatile uint32_t *row = fb + (unsigned)(py * SCREEN_W + x);
        int px;
        for (px = 0; px < w; px++)
            row[px] = color;
    }
    gfx_mark_dirty(x, y, x + w, y + h);
}

/* Gambar garis dari (x1,y1) ke (x2,y2) — algoritma Bresenham. */
void draw_line(int x1, int y1, int x2, int y2, uint32_t color) {
    int dx = x2 - x1, dy = y2 - y1;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;
    int e2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int err = dx - dy;
    while (1) {
        draw_pixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
    }
}

/* -------------------------------------------------------------------
 * Teks
 * ------------------------------------------------------------------- */

/* Gambar satu karakter 8x8 bitmap di piksel (x, y). */
void draw_char_gfx(int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font8x8[(unsigned char)c & 0x7F];
    int row, col;
    for (row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (col = 0; col < 8; col++) {
            uint32_t pixel = (bits & (0x80u >> col)) ? fg : bg;
            draw_pixel(x + col, y + row, pixel);
        }
    }
}

/*
 * Gambar karakter half-width 4x8 dengan subsampling font 8x8.
 * Setiap kolom output mengambil bit ke-7, 5, 3, 1 dari glyph asli.
 */
void draw_char_4x8(int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font8x8[(unsigned char)c & 0x7F];
    int row, col;
    for (row = 0; row < 8; row++) {
        uint8_t b = glyph[row];
        for (col = 0; col < 4; col++) {
            uint32_t pixel = (b & (0x80u >> (col * 2))) ? fg : bg;
            draw_pixel(x + col, y + row, pixel);
        }
    }
}

/* Gambar string dengan font 4x8 (stride 4px per karakter). */
void draw_string_4x8(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) { draw_char_4x8(x, y, *s++, fg, bg); x += 4; }
}

/* Gambar string dengan font 8x8 (stride 8px per karakter). */
void draw_string_gfx(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) { draw_char_gfx(x, y, *s++, fg, bg); x += 8; }
}

/* AT1 — Font 8x16: setiap baris font 8x8 digambar dua kali (row-doubling).
 * Hasil: karakter lebih tinggi (16px) dan lebih mudah dibaca di title bar. */
void draw_char_gfx16(int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font8x8[(unsigned char)c & 0x7F];
    int row, col;
    for (row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (col = 0; col < 8; col++) {
            uint32_t pixel = (bits & (0x80u >> col)) ? fg : bg;
            draw_pixel(x + col, y + row * 2,     pixel);
            draw_pixel(x + col, y + row * 2 + 1, pixel);
        }
    }
}

/* Gambar string dengan font 8x16 (stride 8px per karakter, tinggi 16px). */
void draw_string_gfx16(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) { draw_char_gfx16(x, y, *s++, fg, bg); x += 8; }
}
