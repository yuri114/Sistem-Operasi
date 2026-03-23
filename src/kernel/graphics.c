#include "graphics.h"
#include "font8x8.h"
#include <stdint.h>

/* Alamat LFB runtime — diupdate oleh graphics_set_fb() sebelum graphics_init() */
uint32_t gfx_lfb_addr = 0xE0000000U;

/* Pointer framebuffer — diupdate bersamaan dengan gfx_lfb_addr */
static volatile uint8_t *fb = (volatile uint8_t *)0xE0000000U;

/* Set alamat framebuffer dari hasil PCI scan (vbe_find_lfb()) */
void graphics_set_fb(uint32_t addr) {
    gfx_lfb_addr = addr;
    fb = (volatile uint8_t *)addr;
}

/* Inisialisasi grafis: bersihkan layar dengan warna hitam */
void graphics_init() {
    fill_screen(GFX_BLACK);
}

/* Gambar satu piksel di koordinat (x, y) dengan warna tertentu.
 * Koordinat di luar batas layar diabaikan (clipping). */
void draw_pixel(int x, int y, uint8_t color) {
    if ((unsigned int)x >= SCREEN_W || (unsigned int)y >= SCREEN_H) return;
    fb[y * SCREEN_W + x] = color;
}

/* Isi seluruh layar dengan satu warna — tulis 4 byte sekaligus (uint32_t)
 * untuk mengurangi waktu update dan menghilangkan flicker. */
void fill_screen(uint8_t color) {
    uint32_t *fb32 = (uint32_t *)fb;  /* pakai pointer runtime, bukan hardcode */
    uint32_t val = (uint32_t)color
                 | ((uint32_t)color << 8)
                 | ((uint32_t)color << 16)
                 | ((uint32_t)color << 24);
    int i;
    /* SCREEN_W * SCREEN_H harus habis dibagi 4 (640*480=307200, 307200/4=76800) */
    for (i = 0; i < (SCREEN_W * SCREEN_H / 4); i++)
        fb32[i] = val;
}

/* Gambar persegi panjang terisi warna di (x,y), ukuran w x h piksel */
void fill_rect(int x, int y, int w, int h, uint8_t color) {
    int px, py;
    for (py = y; py < y + h; py++)
        for (px = x; px < x + w; px++)
            draw_pixel(px, py, color);
}

/* Gambar garis lurus dari (x1,y1) ke (x2,y2) — algoritma Bresenham */
void draw_line(int x1, int y1, int x2, int y2, uint8_t color) {
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

/* Gambar satu karakter ASCII menggunakan font 8x8 bitmap di piksel (x, y).
 * fg = warna foreground (karakter), bg = warna background (sel). */
void draw_char_gfx(int x, int y, char c, uint8_t fg, uint8_t bg) {
    const uint8_t *glyph = font8x8[(unsigned char)c & 0x7F];
    int row, col;
    for (row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (col = 0; col < 8; col++) {
            uint8_t pixel = (bits & (0x80u >> col)) ? fg : bg;
            draw_pixel(x + col, y + row, pixel);
        }
    }
}

/* Gambar karakter 4x8 (half-width) dengan subsampling font 8x8.
 * Ambil bit ke-7, 5, 3, 1 (satu bit dari tiap pasang kolom asli).
 * Hasilnya: 80 kolom muat di 320px (320/4=80), bentuk huruf terjaga. */
void draw_char_4x8(int x, int y, char c, uint8_t fg, uint8_t bg) {
    const uint8_t *glyph = font8x8[(unsigned char)c & 0x7F];
    int row, col;
    for (row = 0; row < 8; row++) {
        uint8_t b = glyph[row];
        /* Ambil bit 7, 5, 3, 1 (posisi ganjil dari kiri) */
        for (col = 0; col < 4; col++) {
            uint8_t pixel = (b & (0x80u >> (col * 2))) ? fg : bg;
            draw_pixel(x + col, y + row, pixel);
        }
    }
}

/* Gambar string menggunakan font 4x8 (stride 4px per karakter) */
void draw_string_4x8(int x, int y, const char *s, uint8_t fg, uint8_t bg) {
    while (*s) {
        draw_char_4x8(x, y, *s, fg, bg);
        x += 4;
        s++;
    }
}

/* Gambar string teks di piksel (x, y), karakter berurutan ke kanan (stride 8px) */
void draw_string_gfx(int x, int y, const char *s, uint8_t fg, uint8_t bg) {
    while (*s) {
        draw_char_gfx(x, y, *s, fg, bg);
        x += 8;
        s++;
    }
}
