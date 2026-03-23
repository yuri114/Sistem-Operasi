#ifndef MOUSE_H
#define MOUSE_H
#include <stdint.h>

/* State mouse: koordinat piksel dan status tombol */
typedef struct {
    int x, y;
    uint8_t buttons; /* bit0=kiri, bit1=kanan, bit2=tengah */
} MouseState;

void    mouse_init();           /* init PS/2 mouse, unmask IRQ12, gambar kursor awal */
void    mouse_handler();        /* dipanggil dari IRQ12 (isr.asm) */

int     mouse_get_x();
int     mouse_get_y();
uint8_t mouse_get_buttons();
void    mouse_get_state(MouseState *out); /* isi struct MouseState */

#endif
