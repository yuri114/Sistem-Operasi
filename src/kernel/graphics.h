#ifndef GRAPHICS_H
#define GRAPHICS_H
#include <stdint.h>

/* Resolusi layar — compile-time constants */
#define SCREEN_W  1920
#define SCREEN_H  1080
#define SCREEN_BPP 32

/* Alamat LFB: nilai default, diupdate saat runtime oleh graphics_set_fb().
 * Gunakan FB_ADDR di dalam kernel.c untuk scroll() agar ikut update. */
extern uint32_t gfx_lfb_addr;
#define FB_ADDR gfx_lfb_addr

/* Warna 32bpp True Color (0x00RRGGBB) — cocok untuk VBE 32bpp linear frame buffer */
#define GFX_BLACK    0x00000000u
#define GFX_BLUE     0x000000AAu
#define GFX_GREEN    0x0000AA00u
#define GFX_CYAN     0x0000AAAAu
#define GFX_RED      0x00AA0000u
#define GFX_MAGENTA  0x00AA00AAu
#define GFX_BROWN    0x00AA5500u
#define GFX_LGRAY    0x00AAAAAAu
#define GFX_DGRAY    0x00555555u
#define GFX_LBLUE    0x005555FFu
#define GFX_LGREEN   0x0055FF55u
#define GFX_LCYAN    0x0055FFFFu
#define GFX_LRED     0x00FF5555u
#define GFX_LMAGENTA 0x00FF55FFu
#define GFX_YELLOW   0x00FFFF55u
#define GFX_WHITE    0x00FFFFFFu

/* Struct argumen untuk syscall grafis kompleks */
typedef struct { int x, y, w, h; uint32_t color; } GfxRect;
typedef struct { int x1, y1, x2, y2; uint32_t color; } GfxLine;
typedef struct { int x, y; const char *s; uint32_t fg, bg; } GfxStr;

void graphics_init();
void graphics_set_fb(uint32_t addr); /* set alamat LFB runtime */
void draw_pixel(int x, int y, uint32_t color);
void fill_screen(uint32_t color);
void fill_rect(int x, int y, int w, int h, uint32_t color);
void draw_line(int x1, int y1, int x2, int y2, uint32_t color);
void draw_char_gfx(int x, int y, char c, uint32_t fg, uint32_t bg);
void draw_string_gfx(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void draw_char_4x8(int x, int y, char c, uint32_t fg, uint32_t bg);
void draw_string_4x8(int x, int y, const char *s, uint32_t fg, uint32_t bg);

#endif /* GRAPHICS_H */
