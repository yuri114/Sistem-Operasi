#ifndef DEVICE_H
#define DEVICE_H

#include "driver.h"

// ============================================================
// device.h — Registry untuk semua driver yang terdaftar
// ============================================================

#define DEV_VGA  0   // device id: VGA text output
#define DEV_KBD  1   // device id: keyboard input
#define DEV_MAX  8   // slot driver maksimum

// Daftarkan driver ke slot id tertentu
void dev_register(int id, Driver *drv);

// Tulis string ke device (misal: print ke VGA)
// return: hasil dari driver->write, atau -1 jika id tidak valid
int  dev_write(int id, const char *buf);

// Baca satu char dari device (misal: getkey dari keyboard)
// return: hasil dari driver->read, atau -1 jika id tidak valid
int  dev_read(int id, char *buf);

// Kirim kontrol ke device
// return: hasil dari driver->ioctl, atau -1 jika id tidak valid
int  dev_ioctl(int id, int cmd, int arg);

// Inisialisasi semua driver yang terdaftar
void dev_init_all();

#endif
