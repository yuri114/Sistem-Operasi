#include "drv_vga.h"
#include "driver.h"
#include "graphics.h"  /* GFX_* warna 32bpp */

// Fungsi-fungsi VGA diimplementasikan di kernel.c
// Driver ini membungkus mereka ke interface Driver seragam.

extern void print(const char *str);
extern void set_color(uint32_t fg, uint32_t bg);
extern void clear_screen();
extern uint8_t current_color;

/* Peta 4-bit palette CGA/EGA ke warna 32bpp True Color */
static const uint32_t pal16[16] = {
    GFX_BLACK, GFX_BLUE,     GFX_GREEN,   GFX_CYAN,
    GFX_RED,   GFX_MAGENTA,  GFX_BROWN,   GFX_LGRAY,
    GFX_DGRAY, GFX_LBLUE,    GFX_LGREEN,  GFX_LCYAN,
    GFX_LRED,  GFX_LMAGENTA, GFX_YELLOW,  GFX_WHITE,
};

static int vga_init() {
    // VGA sudah diinisialisasi oleh kernel_main, tidak perlu setup lagi
    return 0;
}

// write: cetak string ke layar via fungsi print() kernel
static int vga_write(const char *buf) {
    if (!buf) return -1;
    print(buf);
    return 0;
}

// read: VGA adalah output-only, tidak ada data untuk dibaca
static int vga_read(char *buf) {
    (void)buf;
    return -1;
}

// ioctl: kontrol khusus VGA
//   VGA_IOCTL_SET_COLOR (0): arg = byte warna langsung (nilai current_color)
//   VGA_IOCTL_CLEAR     (1): clear screen
//   VGA_IOCTL_GET_COLOR (2): return warna saat ini
static int vga_ioctl(int cmd, int arg) {
    if (cmd == 0) {
        // arg: byte warna (0xBF = bg<<4 | fg) — konversi ke 32bpp
        uint8_t fg_idx = arg & 0x0F;
        uint8_t bg_idx = (arg >> 4) & 0x0F;
        set_color(pal16[fg_idx], pal16[bg_idx]);
        return 0;
    }
    if (cmd == 1) {
        clear_screen();
        return 0;
    }
    if (cmd == 2) {
        return (int)current_color;
    }
    return -1;
}

Driver drv_vga = {
    .name  = "vga",
    .init  = vga_init,
    .write = vga_write,
    .read  = vga_read,
    .ioctl = vga_ioctl,
};
