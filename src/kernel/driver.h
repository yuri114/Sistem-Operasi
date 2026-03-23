#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>

// ============================================================
// driver.h — Interface seragam untuk semua device driver
// Setiap driver mengimplementasikan function pointer ini.
// ============================================================

// ioctl command untuk VGA driver (dev_id = DEV_VGA)
#define VGA_IOCTL_SET_COLOR  0  // arg = byte warna (fg | bg<<4)
#define VGA_IOCTL_CLEAR      1  // arg = (diabaikan), clear screen
#define VGA_IOCTL_GET_COLOR  2  // return: warna saat ini

// ioctl command untuk Keyboard driver (dev_id = DEV_KBD)
#define KBD_IOCTL_FLUSH      0  // kosongkan buffer keyboard

typedef struct {
    const char *name;                     // nama driver, e.g. "vga", "kbd"
    int   (*init)  ();                    // inisialisasi hardware
    int   (*write) (const char *buf);     // tulis data (e.g. string ke VGA)
    int   (*read)  (char *buf);           // baca data (e.g. char dari keyboard)
    int   (*ioctl) (int cmd, int arg);    // kontrol khusus (warna, flush, dll)
} Driver;

#endif
