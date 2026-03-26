/* graphics.c — Primitif grafis 32bpp untuk VBE Linear Framebuffer */
#include "graphics.h"
#include "font8x8.h"
#include <stdint.h>

/* -------------------------------------------------------------------
 * State global
 * ------------------------------------------------------------------- */

/* Alamat fisik LFB — diperbarui oleh graphics_set_fb() */
uint32_t gfx_lfb_addr = 0xE0000000U;

/* Pointer tulis langsung ke framebuffer 32bpp */
static volatile uint32_t *fb = (volatile uint32_t *)0xE0000000U;

/* -------------------------------------------------------------------
 * Inisialisasi
 * ------------------------------------------------------------------- */

/* Atur alamat LFB baru (hasil PCI scan vbe_find_lfb). */
void graphics_set_fb(uint32_t addr) {
    gfx_lfb_addr = addr;
    fb           = (volatile uint32_t *)addr;
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
}

/* Isi seluruh layar dengan satu warna. */
void fill_screen(uint32_t color) {
    int i;
    for (i = 0; i < SCREEN_W * SCREEN_H; i++)
        fb[i] = color;
}

/* Isi persegi panjang (x, y, w, h) dengan satu warna. */
void fill_rect(int x, int y, int w, int h, uint32_t color) {
    int px, py;
    for (py = y; py < y + h; py++)
        for (px = x; px < x + w; px++)
            draw_pixel(px, py, color);
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
