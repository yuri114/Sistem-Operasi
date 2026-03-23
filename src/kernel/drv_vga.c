#include "drv_vga.h"
#include "driver.h"

// Fungsi-fungsi VGA diimplementasikan di kernel.c
// Driver ini membungkus mereka ke interface Driver seragam.

extern void print(const char *str);
extern void set_color(uint8_t fg, uint8_t bg);
extern void clear_screen();
extern uint8_t current_color;

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
        // arg: byte warna (0xBF = bg<<4 | fg)
        uint8_t fg = arg & 0x0F;
        uint8_t bg = (arg >> 4) & 0x0F;
        set_color(fg, bg);
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
