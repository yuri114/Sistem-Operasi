#ifndef VBE_H
#define VBE_H
#include <stdint.h>

/*
 * vbe.h — Bochs/QEMU VBE (VGA Display Adapter) lewat port I/O.
 * Tidak memerlukan BIOS INT 10h — bisa dipanggil dari Protected Mode.
 * QEMU dengan -vga std expose register ini di port 0x01CE/0x01CF.
 * LFB (Linear FrameBuffer) selalu berada di 0xE0000000 pada QEMU.
 */

#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01CF

#define VBE_DISPI_INDEX_ID      0
#define VBE_DISPI_INDEX_XRES    1
#define VBE_DISPI_INDEX_YRES    2
#define VBE_DISPI_INDEX_BPP     3
#define VBE_DISPI_INDEX_ENABLE  4

#define VBE_DISPI_DISABLED      0x00
#define VBE_DISPI_ENABLED       0x01
#define VBE_DISPI_LFB_ENABLED   0x40   /* gunakan Linear FrameBuffer */

/* Alamat fisik default LFB — digunakan sebagai fallback jika PCI scan gagal.
 * Nilai aktual diperoleh dari vbe_find_lfb() via PCI BAR2 saat runtime. */
#define VBE_LFB_ADDR_DEFAULT    0xE0000000U

/* Set resolusi dan kedalaman warna via Bochs VBE registers.
 * bpp = 8 (256 warna paletted) untuk mode kita. */
void vbe_set_mode(uint16_t width, uint16_t height, uint16_t bpp);

/* Scan PCI bus untuk mencari Bochs VBE stdvga (VID=0x1234, DID=0x1111)
 * dan membaca alamat BAR2 (Linear FrameBuffer). 
 * Mengembalikan alamat fisik LFB, atau VBE_LFB_ADDR_DEFAULT jika tidak ditemukan. */
uint32_t vbe_find_lfb(void);

#endif /* VBE_H */
