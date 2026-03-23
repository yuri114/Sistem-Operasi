/*
 * mouse.c — PS/2 Mouse Driver (IRQ12, Primary Slave PIC bit 4)
 *
 * Paket PS/2 terdiri dari 3 byte yang dikirim berurutan per IRQ12:
 *   Byte 0: flags  — bit0=kiri, bit1=kanan, bit2=tengah,
 *                    bit3=selalu1, bit4=X-sign, bit5=Y-sign
 *   Byte 1: delta X (signed 8-bit, diperluas dengan bit4 flags)
 *   Byte 2: delta Y (signed 8-bit, diperluas dengan bit5 flags; Y PS/2 terbalik)
 *
 * Kursor perangkat keras digambar langsung ke framebuffer VBE.
 * Latar belakang disimpan 8x8 piksel dan dipulihkan sebelum pindah.
 */
#include "mouse.h"
#include "graphics.h"   /* SCREEN_W, SCREEN_H, gfx_lfb_addr */

/* ------------------------------------------------------------------ */
/* I/O helpers lokal                                                   */
/* ------------------------------------------------------------------ */
static inline void outb_m(uint16_t p, uint8_t v) {
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(p));
}
static inline uint8_t inb_m(uint16_t p) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(p));
    return v;
}

/* ------------------------------------------------------------------ */
/* Akses framebuffer langsung (tidak lewat draw_pixel agar lebih cepat)*/
/* ------------------------------------------------------------------ */
static uint8_t fb_get(int x, int y) {
    if (!gfx_lfb_addr) return 0;
    return ((volatile uint8_t *)gfx_lfb_addr)[(uint32_t)(y * SCREEN_W + x)];
}
static void fb_put(int x, int y, uint8_t c) {
    if (!gfx_lfb_addr) return;
    ((volatile uint8_t *)gfx_lfb_addr)[(uint32_t)(y * SCREEN_W + x)] = c;
}

/* ------------------------------------------------------------------ */
/* Bitmap kursor panah 8x8 (bit7 = piksel kiri, 1 = gambar, 0 = transparan) */
/* ------------------------------------------------------------------ */
static const uint8_t cursor_bmp[8] = {
    0x80,   /* 1000 0000 */
    0xC0,   /* 1100 0000 */
    0xE0,   /* 1110 0000 */
    0xF0,   /* 1111 0000 */
    0xF8,   /* 1111 1000 */
    0xE0,   /* 1110 0000 */
    0x90,   /* 1001 0000 */
    0x08    /* 0000 1000 */
};

/* Outline hitam: piksel yang bersebelahan dengan cursor_bmp */
static const uint8_t cursor_outline[8] = {
    0x40,   /* 0100 0000 */
    0x20,   /* 0010 0000 */
    0x10,   /* 0001 0000 */
    0x0C,   /* 0000 1100 */
    0x06,   /* 0000 0110 */
    0x1A,   /* 0001 1010 */
    0x6C,   /* 0110 1100 */
    0x74    /* 0111 0100 */
};

static uint8_t cursor_bg[8 * 8 + 2 * 8]; /* simpan area 10-baris (outline 1px kiri-kanan) */
#define CSIZE 8  /* ukuran kursor piksel */

static int cursor_visible = 0;
static int cursor_sx = 0, cursor_sy = 0;

static void cursor_erase(void) {
    if (!cursor_visible || !gfx_lfb_addr) return;
    int row, col;
    for (row = 0; row < CSIZE; row++)
        for (col = 0; col < CSIZE; col++)
            fb_put(cursor_sx + col, cursor_sy + row,
                   cursor_bg[row * CSIZE + col]);
    cursor_visible = 0;
}

static void cursor_draw(int x, int y) {
    if (!gfx_lfb_addr) return;
    /* Klem ke batas layar agar tidak keluar */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > SCREEN_W - CSIZE) x = SCREEN_W - CSIZE;
    if (y > SCREEN_H - CSIZE) y = SCREEN_H - CSIZE;
    cursor_sx = x;
    cursor_sy = y;

    /* Simpan latar belakang */
    int row, col;
    for (row = 0; row < CSIZE; row++)
        for (col = 0; col < CSIZE; col++)
            cursor_bg[row * CSIZE + col] = fb_get(x + col, y + row);

    /* Gambar outline hitam dahulu (di bawah), lalu kursor putih */
    for (row = 0; row < CSIZE; row++) {
        for (col = 0; col < CSIZE; col++) {
            if (cursor_outline[row] & (0x80u >> col))
                fb_put(x + col, y + row, GFX_BLACK);
        }
    }
    for (row = 0; row < CSIZE; row++) {
        for (col = 0; col < CSIZE; col++) {
            if (cursor_bmp[row] & (0x80u >> col))
                fb_put(x + col, y + row, GFX_WHITE);
        }
    }
    cursor_visible = 1;
}

/* ------------------------------------------------------------------ */
/* State mouse                                                         */
/* ------------------------------------------------------------------ */
static int     mouse_x       = SCREEN_W / 2;
static int     mouse_y       = SCREEN_H / 2;
static uint8_t mouse_buttons = 0;
static uint8_t mouse_pkt[3];
static int     mouse_cycle   = 0;

int     mouse_get_x()       { return mouse_x; }
int     mouse_get_y()       { return mouse_y; }
uint8_t mouse_get_buttons() { return mouse_buttons; }

void mouse_get_state(MouseState *out) {
    if (!out) return;
    out->x       = mouse_x;
    out->y       = mouse_y;
    out->buttons = mouse_buttons;
}

/* ------------------------------------------------------------------ */
/* Inisialisasi PS/2 (KBC = Keyboard Controller di 0x60/0x64)         */
/* ------------------------------------------------------------------ */
static void kbc_wait_wr(void) {
    int n = 100000;
    while (n-- && (inb_m(0x64) & 0x02));   /* IBF harus 0 sebelum tulis */
}
static void kbc_wait_rd(void) {
    int n = 100000;
    while (n-- && !(inb_m(0x64) & 0x01));  /* OBF harus 1 sebelum baca */
}
static void mouse_send(uint8_t cmd) {
    kbc_wait_wr(); outb_m(0x64, 0xD4); /* byte berikutnya → mouse */
    kbc_wait_wr(); outb_m(0x60, cmd);
}
static uint8_t kbc_recv(void) {
    kbc_wait_rd();
    return inb_m(0x60);
}

void mouse_init(void) {
    /* Kosongkan output buffer KBC yang mungkin masih berisi data lama */
    int i;
    for (i = 0; i < 16 && (inb_m(0x64) & 0x01); i++) inb_m(0x60);

    /* 1. Aktifkan auxiliary (mouse) port */
    kbc_wait_wr(); outb_m(0x64, 0xA8);

    /* 2. Baca command byte controller, set IRQ12 enable + enable aux clock */
    kbc_wait_wr(); outb_m(0x64, 0x20);
    uint8_t cb = kbc_recv();
    cb |=  0x02;  /* bit1: enable IRQ12 */
    cb &= ~0x20;  /* bit5: clear "disable mouse" */
    kbc_wait_wr(); outb_m(0x64, 0x60);
    kbc_wait_wr(); outb_m(0x60, cb);

    /* 3. Set Defaults */
    mouse_send(0xF6); kbc_recv(); /* baca ACK */

    /* 4. Enable Data Reporting (streaming) */
    mouse_send(0xF4); kbc_recv();

    /* 5. Unmask IRQ12 pada slave PIC (bit 4 dari IMR slave = 0xA1) */
    outb_m(0xA1, (uint8_t)(inb_m(0xA1) & ~0x10u));

    /* Gambar kursor awal di tengah layar */
    cursor_draw(mouse_x, mouse_y);
}

/* ------------------------------------------------------------------ */
/* IRQ12 handler — dipanggil dari irq12 di isr.asm                    */
/* ------------------------------------------------------------------ */
void mouse_handler(void) {
    uint8_t data = inb_m(0x60);

    /* Resync: byte pertama paket selalu punya bit3=1 */
    if (mouse_cycle == 0 && !(data & 0x08)) return;

    mouse_pkt[mouse_cycle++] = data;
    if (mouse_cycle < 3) return; /* tunggu paket lengkap */
    mouse_cycle = 0;

    uint8_t flags = mouse_pkt[0];

    /* Delta signed 8-bit; bit 4/5 flags adalah bit sign ke-9 (overflow diabaikan) */
    int dx = (int)(int8_t)mouse_pkt[1];
    int dy = (int)(int8_t)mouse_pkt[2];

    cursor_erase();

    /* Y di PS/2: positif = atas; di framebuffer: positif = bawah → invert */
    mouse_x += dx;
    mouse_y -= dy;

    if (mouse_x < 0)          mouse_x = 0;
    if (mouse_y < 0)          mouse_y = 0;
    if (mouse_x >= SCREEN_W)  mouse_x = SCREEN_W - 1;
    if (mouse_y >= SCREEN_H)  mouse_y = SCREEN_H - 1;

    mouse_buttons = (uint8_t)(flags & 0x07u);

    cursor_draw(mouse_x, mouse_y);
    (void)flags; /* suppress potential warning */
}
