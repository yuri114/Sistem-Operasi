#include "lib.h"

// devtest.c — Demo Phase 23: Device Driver Abstraction
// Mengakses VGA dan keyboard lewat device driver interface
// bukan lewat syscall print/getkey secara langsung.

void _start() {
    // --- Demo 1: dev_write ke VGA ---
    dev_write(DEV_VGA, "[devtest] halo dari device driver!\n");

    // --- Demo 2: ioctl VGA — ubah warna ke hijau ---
    dev_ioctl(DEV_VGA, VGA_IOCTL_SET_COLOR, 0x0A);  // 0x0A = bright green on black
    dev_write(DEV_VGA, "[devtest] teks ini berwarna hijau (via ioctl)\n");

    // --- Demo 3: ioctl VGA — kembalikan warna putih ---
    dev_ioctl(DEV_VGA, VGA_IOCTL_SET_COLOR, 0x0F);  // 0x0F = white on black
    dev_write(DEV_VGA, "[devtest] warna kembali putih\n");

    // --- Demo 4: tampilkan warna saat ini via GET_COLOR ---
    int color = dev_ioctl(DEV_VGA, VGA_IOCTL_GET_COLOR, 0);
    char buf[8];
    dev_write(DEV_VGA, "[devtest] warna saat ini = 0x");
    // print hex manual (2 digit)
    buf[0] = "0123456789ABCDEF"[(color >> 4) & 0xF];
    buf[1] = "0123456789ABCDEF"[color & 0xF];
    buf[2] = '\n';
    buf[3] = '\0';
    dev_write(DEV_VGA, buf);

    // --- Demo 5: baca satu tombol dari keyboard via DEV_KBD ---
    dev_write(DEV_VGA, "[devtest] tekan sembarang tombol...\n");
    char kbuf[4];
    dev_read(DEV_KBD, kbuf);
    dev_write(DEV_VGA, "[devtest] tombol ditekan: ");
    char out[4];
    out[0] = kbuf[0];
    out[1] = '\n';
    out[2] = '\0';
    dev_write(DEV_VGA, out);

    // --- Demo 6: ioctl keyboard — flush buffer ---
    dev_ioctl(DEV_KBD, KBD_IOCTL_FLUSH, 0);
    dev_write(DEV_VGA, "[devtest] buffer keyboard di-flush\n");
    dev_write(DEV_VGA, "[devtest] selesai!\n");

    exit();
}
