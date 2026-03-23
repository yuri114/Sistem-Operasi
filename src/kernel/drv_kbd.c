#include "drv_kbd.h"
#include "driver.h"
#include "keyboard.h"

static int kbd_init() {
    // Keyboard IRQ sudah diaktifkan oleh pic_init() di kernel_main
    return 0;
}

// write: keyboard adalah input-only, tidak bisa ditulis
static int kbd_write(const char *buf) {
    (void)buf;
    return -1;
}

// read: ambil satu karakter dari buffer keyboard.
// buf[0] diisi, buf[1]='\0'. Return 1 jika ada karakter, 0 jika buffer kosong.
static int kbd_read(char *buf) {
    if (!buf) return -1;
    // Aktifkan interrupt agar IRQ1 bisa mengisi buffer, lalu tunggu dengan hlt
    // (hlt lebih efisien dari busy-spin: CPU tidur sampai interrupt berikutnya)
    __asm__ volatile ("sti");
    char c = 0;
    while (c == 0) {
        __asm__ volatile ("hlt");   // tidur sampai ada interrupt (IRQ1 atau IRQ0)
        c = keyboard_getchar();
    }
    buf[0] = c;
    buf[1] = '\0';
    return 1;
}

// ioctl:
//   KBD_IOCTL_FLUSH (0): kosongkan buffer keyboard (tidak ada karakter pending)
static int kbd_ioctl(int cmd, int arg) {
    (void)arg;
    if (cmd == 0) {
        // flush: baca dan buang semua karakter yang masih di buffer
        while (keyboard_getchar() != 0);
        return 0;
    }
    return -1;
}

Driver drv_kbd = {
    .name  = "kbd",
    .init  = kbd_init,
    .write = kbd_write,
    .read  = kbd_read,
    .ioctl = kbd_ioctl,
};
