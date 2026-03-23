#include <stdint.h>
#include "keyboard.h"
#include "pic.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEY_BUFFER_SIZE 64

static char key_buffer[KEY_BUFFER_SIZE];
static int key_head = 0; //index untuk menulis karakter baru
static int key_tail = 0; //index untuk membaca karakter

static void key_push(char c) {
    int next = (key_tail + 1) % KEY_BUFFER_SIZE;
    if (next != key_head) { //pastikan buffer tidak penuh
        key_buffer[key_tail] = c;
        key_tail = next;
    }
}

char keyboard_getchar() {
    if (key_head == key_tail) {
        return 0; //buffer kosong
    }
    char c = key_buffer[key_head];
    key_head = (key_head + 1) % KEY_BUFFER_SIZE;
    return c;
}

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

/* Tabel karakter saat Shift ditekan */
static char scancode_shift_table[]={
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', /* 0x00-0x09 */
    '(', ')', '_', '+', 0,   0,   'Q', 'W', 'E', 'R', /* 0x0A-0x13 */
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0,   0,   /* 0x14-0x1D */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', /* 0x1E-0x27 */
    '"',  '~', 0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N', /* 0x28-0x31 */
    'M', '<', '>', '?',  0,  '*',  0,  ' '            /* 0x32-0x39 */
};

/* Fungsi bantu untuk baca port I/O*/
static inline uint8_t inb(uint16_t port){
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* backspace_char dideklarasikan di kernel.c, dipakai oleh shell.c */

static uint8_t extended = 0;      /* flag extended scancode (0xE0) */
static uint8_t shift_pressed = 0; /* Left Shift (0x2A) / Right Shift (0x36) */

/* Dipanggil dari irq_handler saat ada keyboard interrupt.
 * HANYA melakukan buffering — tidak menggambar ke layar.
 * Shell processing (draw_char_gfx dll.) dilakukan di polling loop kernel_main
 * agar tidak terjadi drawing di dalam interrupt context (penyebab delay + crash). */
void keyboard_handler(){
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    // prefix 0xE0 = extended scancode (arrow keys, dll)
    // HARUS dicek SEBELUM bit-7 check karena 0xE0 = 11100000b (bit 7 = 1)
    if (scancode == 0xE0) {
        extended = 1;
        return;
    }

    /* Deteksi Shift release (sebelum bit-7 check umum) */
    if (scancode == 0xAA || scancode == 0xB6) { // Left/Right Shift dilepas
        shift_pressed = 0;
        return;
    }

    /*Bit 7 = 1 berarti tombol dilepas(key release), abaikan */
    if (scancode & 0x80) {
        extended = 0;
        return;
    }

    /* Deteksi Shift press */
    if (scancode == 0x2A || scancode == 0x36) { // Left/Right Shift ditekan
        shift_pressed = 1;
        return;
    }

    if (extended) {
        extended = 0;
        if (scancode == 0x48)       key_push('\x01'); /* ↑ up */
        else if (scancode == 0x50)  key_push('\x02'); /* ↓ down */
        return;
    }

    if (scancode == 0x0F) { key_push('\x03'); return; } /* Tab */

    if (scancode == 0x0E) {
        key_push('\b');
    }
    else if (scancode == 0x1C) {
        key_push('\n');
    }
    else if (scancode < sizeof(scancode_table)) {
        char c = shift_pressed ? scancode_shift_table[scancode] : scancode_table[scancode];
        if (c != 0) key_push(c);
    }
}

