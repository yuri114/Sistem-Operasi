/* vbe.c — Inisialisasi Bochs VBE Display Adapter via port I/O.
 * Dipanggil dari kernel_main setelah paging diaktifkan dan LFB sudah di-map.
 */
#include "vbe.h"
#include <stdint.h>

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" :: "a"(val), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void vbe_write(uint16_t index, uint16_t data) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA,  data);
}

/* Baca 32-bit register dari PCI config space.
 * bus=0, dev=0-31, func=0-7, offset harus aligned ke 4. */
static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = 0x80000000U
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func <<  8)
                  | ((uint32_t)(offset & 0xFC));
    outl(0xCF8, addr);
    return inl(0xCFC);
}

/*
 * vbe_find_lfb — Scan PCI bus 0 untuk menemukan Bochs VBE stdvga.
 * Bochs VBE: Vendor ID = 0x1234, Device ID = 0x1111.
 *
 * BAR0 (offset 0x10) = VRAM / Linear FrameBuffer — ini yang kita butuhkan.
 * BAR2 (offset 0x18) = MMIO control registers (hanya 4KB) — BUKAN framebuffer.
 * Membaca BAR2 dan menulis 307KB ke sana mengakibatkan machine check → triple fault.
 */
uint32_t vbe_find_lfb(void) {
    uint8_t dev;
    for (dev = 0; dev < 32; dev++) {
        uint32_t id = pci_read32(0, dev, 0, 0x00);
        /* Bochs VBE: vendor=0x1234, device=0x1111 → id word-swapped = 0x11111234 */
        if (id == 0x11111234U) {
            /* BAR0 ada di PCI config offset 0x10
             * Bit 0 = 0 berarti memory BAR; mask 4 bit bawah untuk alamat fisik */
            uint32_t bar0 = pci_read32(0, dev, 0, 0x10);
            uint32_t addr = bar0 & 0xFFFFFFF0U;
            if (addr)
                return addr;
        }
    }
    /* Fallback jika PCI scan gagal */
    return VBE_LFB_ADDR_DEFAULT;
}

/*
 * vbe_set_mode — atur resolusi dan BPP melalui Bochs VBE.
 * Prosedur: disable → set XRES/YRES/BPP → enable + LFB.
 */
void vbe_set_mode(uint16_t width, uint16_t height, uint16_t bpp) {
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    vbe_write(VBE_DISPI_INDEX_XRES,   width);
    vbe_write(VBE_DISPI_INDEX_YRES,   height);
    vbe_write(VBE_DISPI_INDEX_BPP,    bpp);
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
}

