#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* Dari kernel.c - untuk menampilkan karakter*/
void print_char(char c);
void keyboard_handler();
char keyboard_getchar(); //fungsi untuk mengambil karakter dari buffer keyboard, mengembalikan 0 jika buffer kosong

#endif
