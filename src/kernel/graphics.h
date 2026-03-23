#ifndef GRAPHICS_H
#define GRAPHICS_H
#include <stdint.h>

/* Resolusi layar — compile-time constants */
#define SCREEN_W  640
#define SCREEN_H  480

/* Alamat LFB: nilai default, diupdate saat runtime oleh graphics_set_fb().
 * Gunakan FB_ADDR di dalam kernel.c untuk scroll() agar ikut update. */
extern uint32_t gfx_lfb_addr;
#define FB_ADDR gfx_lfb_addr

/* Warna standar VGA Mode 13h (16 warna pertama = palette CGA/EGA) */
#define GFX_BLACK     0
#define GFX_BLUE      1
#define GFX_GREEN     2
#define GFX_CYAN      3
#define GFX_RED       4
#define GFX_MAGENTA   5
#define GFX_BROWN     6
#define GFX_LGRAY     7
#define GFX_DGRAY     8
#define GFX_LBLUE     9
#define GFX_LGREEN   10
#define GFX_LCYAN    11
#define GFX_LRED     12
#define GFX_LMAGENTA 13
#define GFX_YELLOW   14
#define GFX_WHITE    15

/* Struct argumen untuk syscall grafis kompleks */
typedef struct { int x, y, w, h; uint8_t color; } GfxRect;
typedef struct { int x1, y1, x2, y2; uint8_t color; } GfxLine;
typedef struct { int x, y; const char *s; uint8_t fg, bg; } GfxStr;

void graphics_init();
void graphics_set_fb(uint32_t addr); /* set alamat LFB runtime */
void draw_pixel(int x, int y, uint8_t color);
void fill_screen(uint8_t color);
void fill_rect(int x, int y, int w, int h, uint8_t color);
void draw_line(int x1, int y1, int x2, int y2, uint8_t color);
void draw_char_gfx(int x, int y, char c, uint8_t fg, uint8_t bg);
void draw_string_gfx(int x, int y, const char *s, uint8_t fg, uint8_t bg);
void draw_char_4x8(int x, int y, char c, uint8_t fg, uint8_t bg);
void draw_string_4x8(int x, int y, const char *s, uint8_t fg, uint8_t bg);

#endif /* GRAPHICS_H */
