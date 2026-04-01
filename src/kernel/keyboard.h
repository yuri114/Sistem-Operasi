#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* Kontrol karakter untuk tombol khusus */
#define KEY_UP      0x01    /* Arrow Up    */
#define KEY_DOWN    0x02    /* Arrow Down  */
#define KEY_TAB     0x03    /* Tab         */
#define KEY_LEFT    0x04    /* Arrow Left  */
#define KEY_RIGHT   0x05    /* Arrow Right */
#define KEY_DELETE  0x06    /* Delete      */
#define KEY_F1      0x10    /* F1          */
#define KEY_F2      0x11    /* F2          */
#define KEY_F3      0x12    /* F3          */
#define KEY_F4      0x13    /* F4          */
#define KEY_F5      0x14    /* F5          */
#define KEY_F6      0x15    /* F6          */
#define KEY_F7      0x16    /* F7          */
#define KEY_F8      0x17    /* F8          */
#define KEY_F9      0x18    /* F9          */
#define KEY_F10     0x19    /* F10         */

void print_char(char c);
void keyboard_handler();
char keyboard_getchar();

/* Kembalikan 1 jika tombol Ctrl sedang ditekan */
int keyboard_ctrl_pressed();
/* Kembalikan 1 jika tombol Alt sedang ditekan */
int keyboard_alt_pressed();
/* Kembalikan 1 jika Caps Lock aktif */
int keyboard_capslock_state();

#endif
