#ifndef SERIAL_H
#define SERIAL_H

/* serial.h — Driver COM1 untuk debug output via QEMU -serial stdio */

void serial_init();
void serial_putchar(char c);
void serial_print(const char *s);
void serial_print_hex(unsigned long long v);

#endif
