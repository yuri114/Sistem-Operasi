/* gfxtest.c — Demo Phase 24: VGA Mode 13h Graphics
 * Program user untuk mendemonstrasikan primitif grafis:
 * - Palet 16 warna
 * - Garis (Bresenham)
 * - Persegi panjang terisi
 * - Teks dalam mode grafis
 */
#include "lib.h"

void _start() {
    /* Bersihkan layar, kursor ke sudut kiri atas */
    gfx_clear();

    /* Buang semua tombol pending (termasuk Enter dari shell) */
    dev_ioctl(DEV_KBD, KBD_IOCTL_FLUSH, 0);

    /* Ubah warna teks: hijau terang di atas hitam */
    dev_ioctl(DEV_VGA, VGA_IOCTL_SET_COLOR, GFX_LGREEN | (GFX_BLACK << 4));
    print("=== Phase 24: VGA Mode 13h ===\n");
    print("Resolusi: 320 x 200, 256 warna\n");

    /* Kembalikan ke warna standar */
    dev_ioctl(DEV_VGA, VGA_IOCTL_SET_COLOR, GFX_WHITE | (GFX_BLACK << 4));
    print("Palette 16 warna (baris atas):\n");

    /* ---- Palet 16 warna: 16 blok 20x18 piksel ---- */
    int i;
    for (i = 0; i < 16; i++) {
        GfxRect r;
        r.x = i * 20; r.y = 28; r.w = 20; r.h = 18;
        r.color = (unsigned char)i;
        gfx_rect_s(&r);
    }

    /* Garis pemisah */
    GfxLine sep;
    sep.x1 = 0; sep.y1 = 48; sep.x2 = 319; sep.y2 = 48;
    sep.color = GFX_LGRAY;
    gfx_line_s(&sep);

    /* ---- Teks keterangan ---- */
    dev_ioctl(DEV_VGA, VGA_IOCTL_SET_COLOR, GFX_YELLOW | (GFX_BLACK << 4));
    print("\nContoh primitif grafis:\n");
    dev_ioctl(DEV_VGA, VGA_IOCTL_SET_COLOR, GFX_LGRAY | (GFX_BLACK << 4));

    /* ---- 5 Persegi panjang warna-warni ---- */
    GfxRect rr; rr.x=8;   rr.y=72; rr.w=60; rr.h=36; rr.color=GFX_RED;      gfx_rect_s(&rr);
    GfxRect rg; rg.x=80;  rg.y=72; rg.w=60; rg.h=36; rg.color=GFX_LGREEN;   gfx_rect_s(&rg);
    GfxRect rb; rb.x=152; rb.y=72; rb.w=60; rb.h=36; rb.color=GFX_LBLUE;    gfx_rect_s(&rb);
    GfxRect ry; ry.x=224; ry.y=72; ry.w=60; ry.h=36; ry.color=GFX_YELLOW;   gfx_rect_s(&ry);
    GfxRect rm; rm.x=296; rm.y=72; rm.w=24; rm.h=36; rm.color=GFX_LMAGENTA; gfx_rect_s(&rm);

    /* ---- Garis silang ---- */
    GfxLine la; la.x1=0;   la.y1=120; la.x2=319; la.y2=175; la.color=GFX_CYAN;   gfx_line_s(&la);
    GfxLine lb; lb.x1=319; lb.y1=120; lb.x2=0;   lb.y2=175; lb.color=GFX_LRED;   gfx_line_s(&lb);
    GfxLine lc; lc.x1=0;   lc.y1=175; lc.x2=319; lc.y2=120; lc.color=GFX_YELLOW; gfx_line_s(&lc);

    /* Bingkai batas area grafis */
    GfxLine lt;  lt.x1=0;  lt.y1=118; lt.x2=319; lt.y2=118; lt.color=GFX_LGRAY; gfx_line_s(&lt);
    GfxLine lb2; lb2.x1=0; lb2.y1=177;lb2.x2=319;lb2.y2=177;lb2.color=GFX_LGRAY; gfx_line_s(&lb2);

    /* ---- Teks penutup ---- */
    dev_ioctl(DEV_VGA, VGA_IOCTL_SET_COLOR, GFX_WHITE | (GFX_BLACK << 4));
    print("\n\nTekan sembarang tombol untuk kembali...");

    char key[2];
    dev_read(DEV_KBD, key);

    /* Bersihkan dan kembalikan warna default */
    dev_ioctl(DEV_VGA, VGA_IOCTL_SET_COLOR, GFX_LGRAY | (GFX_BLACK << 4));
    gfx_clear();
    exit();
}

