#ifndef WALLPAPER_H
#define WALLPAPER_H

/* wallpaper.h — Desktop wallpaper dari ATA secondary master
 *
 * Format disk: raw BGRA32 960×540 piksel (konversi dari PNG saat build).
 * Disimpan di wallpaper.img (QEMU index=2, secondary master, 0x170).
 * Di-blit ke framebuffer 1920×1080 sebagai blok 2×2 piksel per sumber.
 */

/* Resolusi sumber wallpaper yang disimpan di disk */
#define WP_SRC_W   960
#define WP_SRC_H   540

/* Coba baca dan blit wallpaper ke framebuffer.
 * Return 1 jika berhasil, 0 jika drive tidak ada / error. */
int wp_blit(void);

/* Return 1 jika wallpaper sudah berhasil di-load */
int wp_is_loaded(void);

#endif /* WALLPAPER_H */
