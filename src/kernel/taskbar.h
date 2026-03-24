#ifndef TASKBAR_H
#define TASKBAR_H
#include <stdint.h>

#define TASKBAR_H_PX  16   /* tinggi taskbar dalam piksel (di bawah layar) */

/* Render taskbar ke framebuffer — dipanggil dari wm_redraw_all() */
void taskbar_draw(void);

/* Proses klik mouse di area taskbar — dipanggil dari wm_mouse_event() */
void taskbar_click(int mx, int my);

#endif
