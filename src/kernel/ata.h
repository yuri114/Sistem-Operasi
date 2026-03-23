#ifndef ATA_H
#define ATA_H
#include <stdint.h>

/* Inisialisasi driver ATA PIO — deteksi keberadaan disk sekunder */
void ata_init();

/* Kembalikan 1 jika disk sekunder (Primary Slave) terdeteksi, 0 jika tidak */
int ata_disk_present();

/* Baca 1 sektor (512 byte) dari LBA ke buf.  Return 0 sukses, -1 gagal. */
int ata_read_sector(uint32_t lba, uint8_t *buf);

/* Tulis 1 sektor (512 byte) dari buf ke LBA.  Return 0 sukses, -1 gagal. */
int ata_write_sector(uint32_t lba, const uint8_t *buf);

#endif
