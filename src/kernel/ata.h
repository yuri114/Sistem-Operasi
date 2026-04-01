#ifndef ATA_H
#define ATA_H
#include <stdint.h>

/* Tipe drive yang dapat dideteksi via IDENTIFY */
typedef enum {
    ATA_DRIVE_NONE  = 0,  /* Tidak ada drive / tidak dikenali */
    ATA_DRIVE_HDD   = 1,  /* ATA hard disk */
    ATA_DRIVE_ATAPI = 2   /* ATAPI (CDROM, dll.) */
} ata_drive_type_t;

/* Inisialisasi driver: deteksi BMIDE via PCI, deteksi tipe drive via IDENTIFY */
void ata_init(void);

/* Kembalikan 1 jika ATA HDD sekunder terdeteksi dan siap dipakai */
int ata_disk_present(void);

/* Kembalikan 1 jika Bus Master DMA tersedia (PIIX3/PIIX4 ditemukan) */
int ata_dma_available(void);

/* Kembalikan tipe drive yang terdeteksi */
ata_drive_type_t ata_get_drive_type(void);

/* Baca 1 sektor (512 byte) dari LBA ke buf.
 * Otomatis pakai DMA jika tersedia, fallback ke PIO.
 * Return 0 sukses, -1 gagal (termasuk LBA > 0x0FFFFFFF). */
int ata_read_sector(uint32_t lba, uint8_t *buf);

/* Tulis 1 sektor (512 byte) dari buf ke LBA.
 * Otomatis pakai DMA jika tersedia, fallback ke PIO.
 * Return 0 sukses, -1 gagal. */
int ata_write_sector(uint32_t lba, const uint8_t *buf);

#endif
