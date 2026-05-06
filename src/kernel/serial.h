#ifndef SERIAL_H
#define SERIAL_H

/* serial.h — Driver COM1 untuk debug output via QEMU -serial stdio */

void serial_init(void);
void serial_putchar(char c);
void serial_print(const char *s);
void serial_print_hex(unsigned long long v);

/* Fondasi AS — Kernel Debugger */

/* Terima satu karakter dari COM1 (blocking). */
char serial_getchar(void);

/* Non-blocking: return 1 jika ada karakter tersedia di COM1. */
int  serial_haschar(void);

/* Kernel debugger interaktif via serial.
 * Ketik perintah di terminal serial (QEMU: -serial stdio / -serial telnet:...).
 * Perintah: help, x <addr> [n], bt, r, q */
void kdbg_run(void);

/* ================================================================
 * Fondasi AZ — dmesg: kernel ring buffer log
 * ================================================================ */
#define DMESG_BUF_SIZE 4096

/* Tambahkan pesan ke ring buffer dmesg. */
void dmesg_log(const char *s);

/* Salin isi ring buffer ke buf, max maxlen-1 byte, null-terminated.
 * Return panjang yang disalin. */
int  dmesg_read(char *buf, int maxlen);

#endif /* SERIAL_H */
