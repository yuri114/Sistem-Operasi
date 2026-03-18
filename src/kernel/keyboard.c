#include <stdint.h>
#include "keyboard.h"
#include "pic.h"

#define KEYBOARD_DATA_PORT 0x60

/*  Tabel scancode karakter ASCII
    Index = scancode, value = karakter
    0 = tidak ada karakter (shift, ctrl, dll)*/
static char scancode_table[]={
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', /* 0x00-0x09 */
    '9', '0', '-', '=', 0,   0,   'q', 'w', 'e', 'r', /* 0x0A-0x13 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,   0,   /* 0x14-0x1D */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', /* 0x1E-0x27 */
    '\'', '`', 0,  '\\','z', 'x', 'c', 'v', 'b', 'n', /* 0x28-0x31 */
    'm', ',', '.', '/',  0,  '*',  0,' '               /* 0x32-0x39 */
};

/* Fungsi bantu untuk baca port I/O*/
static inline uint8_t inb(uint16_t port){
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Dipanggil dari irq_handler saat ada keyboard interrupt */
void keyboard_handler(){
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    /*Bit 7 = 1 berarti tombol dilepas(key release), abaikan */
    if (scancode & 0x80) {
        return;
    }
    /* cek apakah scancode ada di table */
    if (scancode < sizeof(scancode_table)) {
        char c = scancode_table[scancode];
        if (c!=0) {
            print_char(c); /* Tampilkan karakter ke layar */
        }
    }
}

