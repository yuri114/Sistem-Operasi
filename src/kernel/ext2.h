#ifndef EXT2_H
#define EXT2_H
/* ext2.h — EXT2 Filesystem read-only driver
 * Fondasi AR — Mount/read EXT2 disk image dari ATA secondary drive
 *
 * Sumber data: ATA secondary IDE (index=1, LBA 0+)
 * Penggunaan:
 *   ext2_mount()       — baca superblock, validasi magic 0xEF53
 *   ext2_ls(path)      — list isi direktori
 *   ext2_cat(path)     — cetak isi file ke layar
 *   ext2_mounted()     — 1 jika sudah di-mount
 */
#include <stdint.h>

/* Inisialisasi dan mount EXT2 dari ATA secondary drive.
 * Return 1 sukses (EXT2 valid), 0 gagal (disk kosong / bukan EXT2). */
int  ext2_mount(void);

/* Return 1 jika EXT2 sudah di-mount. */
int  ext2_is_mounted(void);

/* List isi direktori. path relatif dari root EXT2, contoh "/" atau "/etc".
 * Cetak ke layar. */
void ext2_ls(const char *path);

/* Baca isi file dan cetak ke layar. path contoh "/etc/passwd". */
void ext2_cat(const char *path);

#endif /* EXT2_H */
