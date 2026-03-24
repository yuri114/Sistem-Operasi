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

/* Update cursor_bg ketika pixel ditulis langsung ke framebuffer.
 * Pastikan cursor_erase() nanti me-restore warna yang benar. */
void cursor_update_pixel(int x, int y, uint8_t color);
void cursor_update_region(int x, int y, int w, int h, uint8_t color);

#endif
