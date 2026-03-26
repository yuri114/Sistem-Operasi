/* keyboard.c — Driver keyboard PS/2 (IRQ1), buffer ring, tab/arrow support */
#include <stdint.h>
#include "keyboard.h"
#include "pic.h"

#define KEYBOARD_DATA_PORT  0x60
#define KEY_BUFFER_SIZE     64

static char key_buffer[KEY_BUFFER_SIZE];
static int  key_head = 0;   /* indeks baca */
static int  key_tail = 0;   /* indeks tulis */

/* -------------------------------------------------------------------
 * Buffer ring
 * ------------------------------------------------------------------- */

static void key_push(char c) {
    int next = (key_tail + 1) % KEY_BUFFER_SIZE;
    if (next != key_head) {     /* abaikan jika buffer penuh */
        key_buffer[key_tail] = c;
        key_tail = next;
    }
}

/* Ambil satu karakter dari buffer; kembalikan 0 jika kosong. */
char keyboard_getchar() {
    if (key_head == key_tail) return 0;
    char c    = key_buffer[key_head];
    key_head  = (key_head + 1) % KEY_BUFFER_SIZE;
    return c;
}

/* -------------------------------------------------------------------
 * Tabel scancode → ASCII (index = scancode, 0 = tidak ada karakter)
 * ------------------------------------------------------------------- */
static char scancode_table[] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8',   /* 0x00–0x09 */
    '9', '0', '-', '=', 0,   0,   'q', 'w', 'e', 'r',   /* 0x0A–0x13 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,   0,     /* 0x14–0x1D */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',   /* 0x1E–0x27 */
    '\'','`', 0,  '\\','z', 'x', 'c', 'v', 'b', 'n',   /* 0x28–0x31 */
    'm', ',', '.', '/',  0,  '*',  0,  ' '               /* 0x32–0x39 */
};

static char scancode_shift_table[] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*',   /* 0x00–0x09 */
    '(', ')', '_', '+', 0,   0,   'Q', 'W', 'E', 'R',   /* 0x0A–0x13 */
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0,   0,     /* 0x14–0x1D */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',   /* 0x1E–0x27 */
    '"', '~', 0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N',   /* 0x28–0x31 */
    'M', '<', '>', '?',  0,  '*',  0,  ' '              /* 0x32–0x39 */
};

/* -------------------------------------------------------------------
 * Handler IRQ1
 *
 * Hanya buffering — tidak menggambar ke layar.
 * Shell polling loop di kernel_main yang melakukan rendering.
 * ------------------------------------------------------------------- */
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static uint8_t extended     = 0;   /* flag prefix 0xE0 (arrow keys, dll) */
static uint8_t shift_pressed = 0;  /* 1 = Left/Right Shift sedang ditekan */

void keyboard_handler() {
    uint8_t sc = inb(KEYBOARD_DATA_PORT);

    /* 0xE0 = extended scancode prefix (arrow keys, Insert, dll) */
    if (sc == 0xE0) { extended = 1; return; }

    /* Shift release: 0xAA = Left Shift up, 0xB6 = Right Shift up */
    if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; return; }

    /* Bit 7 = 1: key release — abaikan (kecuali Shift sudah ditangani atas) */
    if (sc & 0x80) { extended = 0; return; }

    /* Shift press */
    if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; return; }

    /* Extended key: arrow keys dikirim sebagai karakter kontrol */
    if (extended) {
        extended = 0;
        if      (sc == 0x48) key_push('\x01');  /* ↑ up    */
        else if (sc == 0x50) key_push('\x02');  /* ↓ down  */
        else if (sc == 0x4B) key_push('\x04');  /* ← left  */
        else if (sc == 0x4D) key_push('\x05');  /* → right */
        return;
    }

    if (sc == 0x0F) { key_push('\x03'); return; }  /* Tab */

    if      (sc == 0x0E) key_push('\b');            /* Backspace */
    else if (sc == 0x1C) key_push('\n');            /* Enter */
    else if (sc < sizeof(scancode_table)) {
        char c = shift_pressed ? scancode_shift_table[sc] : scancode_table[sc];
        if (c) key_push(c);
    }
}

