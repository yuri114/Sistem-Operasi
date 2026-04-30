/*
 * wallpaper.c — Desktop wallpaper loader dari ATA secondary master (0x170)
 *
 * Drive: QEMU -drive index=2,if=ide  → secondary master, port 0x170
 *        Drive/Head = 0xE0 (LBA mode, master)
 *
 * Format data: raw BGRA32 pada sektor 0..4049 (960×540×4 = 2,073,600 byte)
 * Blit ke FB 1920×1080 dengan skala 2×2 piksel (nearest-neighbor).
 *
 * Cache: piksel sumber di-malloc() ~2MB di heap saat pertama kali dipanggil.
 * wp_blit() membaca disk SEKALI, lalu blit dari cache (hanya memory copy).
 */
#include "wallpaper.h"
#include "graphics.h"
#include "memory.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* ATA Secondary Channel (0x170)                                        */
/* ------------------------------------------------------------------ */
#define WP_DATA     0x170u
#define WP_ERROR    0x171u
#define WP_SECT_CNT 0x172u
#define WP_LBA_LO   0x173u
#define WP_LBA_MID  0x174u
#define WP_LBA_HI   0x175u
#define WP_DRIVE    0x176u
#define WP_CMD      0x177u
#define WP_ALT_STAT 0x376u

/* Secondary MASTER, LBA mode: bit7=1, bit6=1(LBA), bit5=1, bit4=0(master) */
#define WP_DRIVE_SEL 0xE0u

#define ATA_CMD_READ 0x20u
#define ATA_BSY      0x80u
#define ATA_DRQ      0x08u
#define ATA_ERR      0x01u

/* ------------------------------------------------------------------ */
/* Port I/O helpers                                                     */
/* ------------------------------------------------------------------ */
static inline void wp_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t wp_inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline uint16_t wp_inw(uint16_t port) {
    uint16_t r;
    __asm__ volatile ("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

/* 400ns delay: baca alt-status 4× */
static void wp_delay(void) {
    wp_inb(WP_ALT_STAT); wp_inb(WP_ALT_STAT);
    wp_inb(WP_ALT_STAT); wp_inb(WP_ALT_STAT);
}

/* Tunggu BSY=0 dan DRQ=1. Return 0=OK, -1=error/timeout */
static int wp_wait_drq(void) {
    int t = 500000;
    while (t--) {
        uint8_t st = wp_inb(WP_CMD);
        if (st & ATA_ERR)  return -1;
        if (!(st & ATA_BSY) && (st & ATA_DRQ)) return 0;
    }
    return -2;
}

/* Tunggu BSY=0. Return 0=OK, -1=timeout */
static int wp_wait_ready(void) {
    int t = 500000;
    while ((wp_inb(WP_CMD) & ATA_BSY) && t--);
    return (t > 0) ? 0 : -1;
}

/* Baca 1 sektor (512 byte) dari LBA ke buf.
 * Return 0=OK, -1=error */
static int wp_read_sector(uint32_t lba, uint8_t *buf) {
    if (wp_wait_ready() < 0) return -1;

    wp_outb(WP_DRIVE,    (uint8_t)(WP_DRIVE_SEL | ((lba >> 24) & 0x0Fu)));
    wp_delay();
    wp_outb(WP_SECT_CNT, 1);
    wp_outb(WP_LBA_LO,   (uint8_t)(lba));
    wp_outb(WP_LBA_MID,  (uint8_t)(lba >> 8));
    wp_outb(WP_LBA_HI,   (uint8_t)(lba >> 16));
    wp_outb(WP_CMD,      ATA_CMD_READ);

    wp_delay();
    if (wp_wait_drq() < 0) return -1;

    /* Baca 256 word (512 byte) */
    uint16_t *w = (uint16_t *)(uintptr_t)buf;
    int i;
    for (i = 0; i < 256; i++)
        w[i] = wp_inw(WP_DATA);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Deteksi drive: cek signature register setelah select               */
/* ------------------------------------------------------------------ */
static int wp_drive_present(void) {
    if (wp_wait_ready() < 0) return 0;
    wp_outb(WP_DRIVE, WP_DRIVE_SEL);
    wp_delay();
    /* Jika LBA_MID atau LBA_HI bukan 0 setelah reset biasanya ATAPI/tidak ada */
    wp_outb(WP_SECT_CNT, 0xAB);  /* tulis pattern */
    uint8_t readback = wp_inb(WP_SECT_CNT);
    return (readback == 0xAB) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* State & heap cache                                                   */
/* ------------------------------------------------------------------ */
static int       g_wp_loaded = 0;   /* 1 = cache valid */
static uint32_t *g_wp_cache  = 0;   /* heap: WP_SRC_W*WP_SRC_H pixel 0xRRGGBB */

/* Buffer sektor sementara (512 byte, statis agar tidak di stack) */
static uint8_t g_sect_buf[512];

/* ------------------------------------------------------------------ */
/* Blit cache ke framebuffer — hanya memory copy, sangat cepat        */
/* Tidak ada modulo/divisi: pakai x/y counter langsung                */
/* ------------------------------------------------------------------ */
static void wp_blit_cache(void) {
    extern uint32_t gfx_lfb_addr;
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)gfx_lfb_addr;
    uint32_t *cache = g_wp_cache;
    uint32_t sy;
    for (sy = 0; sy < (uint32_t)WP_SRC_H; sy++) {
        uint32_t fb_row = sy * 2u * 1920u;
        uint32_t sx;
        for (sx = 0; sx < (uint32_t)WP_SRC_W; sx++) {
            uint32_t color = *cache++;
            uint32_t base  = fb_row + sx * 2u;
            fb[base]            = color;
            fb[base + 1]        = color;
            fb[base + 1920]     = color;
            fb[base + 1920 + 1] = color;
        }
    }
}

/* ------------------------------------------------------------------ */
/* wp_blit_region — blit hanya sebagian layar dari cache               */
/* Koordinat dalam piksel layar (1920×1080).                           */
/* ------------------------------------------------------------------ */
void wp_blit_region(int dx, int dy, int dw, int dh) {
    if (!g_wp_loaded || !g_wp_cache) return;
    extern uint32_t gfx_lfb_addr;
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)gfx_lfb_addr;
    /* Clamp ke batas layar */
    if (dx < 0)        { dw += dx; dx = 0; }
    if (dy < 0)        { dh += dy; dy = 0; }
    if (dx + dw > 1920) dw = 1920 - dx;
    if (dy + dh > 1080) dh = 1080 - dy;
    if (dw <= 0 || dh <= 0) return;
    int sy;
    for (sy = dy; sy < dy + dh; sy++) {
        /* Baris cache = sy/2 (tiap cache row diulang 2× vertikal) */
        uint32_t *cache_row = g_wp_cache + (uint32_t)(sy >> 1) * (uint32_t)WP_SRC_W;
        uint32_t  fb_row    = (uint32_t)sy * 1920u;
        int sx;
        for (sx = dx; sx < dx + dw; sx++) {
            /* Kolom cache = sx/2 (tiap cache col diulang 2× horizontal) */
            fb[fb_row + (uint32_t)sx] = cache_row[sx >> 1];
        }
    }
}

/* ------------------------------------------------------------------ */
/* wp_blit() — baca disk SEKALI, lalu blit dari cache                  */
/* ------------------------------------------------------------------ */
/*
 * Format disk: setiap pixel = 4 byte [B, G, R, A] (BGRA32 little-endian).
 * Framebuffer: 0x00RRGGBB.
 * Skala: 1 pixel sumber → 2×2 pixel di layar 1920×1080.
 * Total sektor: ceil(960×540×4 / 512) = 4050 sektor.
 */
int wp_blit(void) {
    if (!g_wp_loaded) {
        /* Pertama kali: alokasikan cache di heap (~2MB) lalu baca disk */
        if (!wp_drive_present()) return 0;

        uint32_t total_px = (uint32_t)WP_SRC_W * WP_SRC_H;
        if (!g_wp_cache) {
            g_wp_cache = (uint32_t *)malloc(total_px * sizeof(uint32_t));
            if (!g_wp_cache) return 0;   /* OOM — fallback ke solid */
        }

        uint32_t lba         = 0;
        uint32_t pixels_done = 0;

        while (pixels_done < total_px) {
            if (wp_read_sector(lba, g_sect_buf) < 0) return 0;
            lba++;

            int pi;
            for (pi = 0; pi < 128 && pixels_done < total_px; pi++, pixels_done++) {
                int byte_off = pi * 4;
                uint8_t b  = g_sect_buf[byte_off];
                uint8_t gg = g_sect_buf[byte_off + 1];
                uint8_t r  = g_sect_buf[byte_off + 2];
                g_wp_cache[pixels_done] =
                    ((uint32_t)r << 16) | ((uint32_t)gg << 8) | b;
            }
        }
        g_wp_loaded = 1;
    }

    /* Cache sudah terisi — blit cepat dari RAM */
    wp_blit_cache();
    return 1;
}

int wp_is_loaded(void) { return g_wp_loaded; }
