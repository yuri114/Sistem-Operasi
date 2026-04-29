#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H
/* virtio_blk.h — Fondasi AO: VirtIO Block Device Driver */
#include <stdint.h>

/* Inisialisasi: scan PCI untuk VirtIO blk (vendor 0x1AF4, dev 0x1001/0x1042).
 * Jika ditemukan, setup virtqueue dan print info ke layar. */
void virtio_blk_init(void);

/* Return 1 jika VirtIO blk berhasil diinit, 0 jika tidak ada. */
int  virtio_blk_present(void);

/* Baca nsectors sektor 512-byte mulai dari sector ke buf.
 * Return 0 sukses, negatif jika error. */
int  virtio_blk_read(uint64_t sector, uint8_t *buf, uint32_t nsectors);

/* Tulis nsectors sektor 512-byte dari buf ke sector.
 * Return 0 sukses, negatif jika error. */
int  virtio_blk_write(uint64_t sector, const uint8_t *buf, uint32_t nsectors);

/* Return kapasitas disk dalam sektor (512 bytes each). */
uint64_t virtio_blk_capacity(void);

#endif /* VIRTIO_BLK_H */
