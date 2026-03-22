#include <stdint.h>
#include "keyboard.h"
#include "pic.h"
#include "shell.h"

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

/* Fungsi bantu untuk baca port I/O*/
static inline uint8_t inb(uint16_t port){
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void backspace_char();

static uint8_t extended = 0; // flag untuk extended scancode (0xE0)

/* Dipanggil dari irq_handler saat ada keyboard interrupt */
void keyboard_handler(){
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    // prefix 0xE0 = extended scancode (arrow keys, dll)
    // HARUS dicek SEBELUM bit-7 check karena 0xE0 = 11100000b (bit 7 = 1)
    if (scancode == 0xE0) {
        extended = 1;
        return;
    }

    /*Bit 7 = 1 berarti tombol dilepas(key release), abaikan */
    if (scancode & 0x80) {
        extended = 0;
        return;
    }

    if (extended) {
        extended = 0;
        if (scancode == 0x48) {          // ↑ up arrow
            shell_process_char('\x01');
        } else if (scancode == 0x50) {   // ↓ down arrow
            shell_process_char('\x02');
        }
        return;
    }

    if (scancode == 0x0E) { //scancode 0x0E = backspace
        key_push('\b'); //push karakter backspace ke buffer
        shell_process_char('\b');
    }
    else if (scancode == 0x1C){
        key_push('\n'); //push karakter newline ke buffer
        shell_process_char('\n');
    }
    else if (scancode < sizeof(scancode_table)) {
        char c = scancode_table[scancode];
        if (c != 0) {
            key_push(c); //push karakter ke buffer
            shell_process_char(c); /* Tampilkan karakter ke layar */
        }
    }
}

