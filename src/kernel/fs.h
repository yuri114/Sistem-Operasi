#ifndef FS_H
#define FS_H
#include <stdint.h>

/* ============================================================
 * MFS3 — MyFS versi 3
 *   64 file, nama maks 32 karakter, maks 64KB per file.
 *   Data disimpan via pointer heap (bukan embedded array)
 *   sehingga metadata array hanya ~4KB.
 *
 * Disk layout (Primary Slave):
 *   Sector 0                  : superblock (magic 'MFS3', reserved)
 *   Sector 1+i*129            : header file ke-i (512 byte)
 *   Sector 2+i*129 .. 129+i*129 : data file ke-i (128 × 512 = 64KB)
 *   Total: 1 + 64×129 = 8257 sektor ≈ 4.1MB  (fits in 8MB disk)
 * ============================================================ */

#define FS_MAX_FILES 64
#define FS_MAX_NAME  32
#define FS_MAX_DATA  65536          /* 128 sektor × 512 bytes = 64KB per file */
#define FS_DATA_SECS (FS_MAX_DATA / 512)   /* 128 sektor per file */

/* Bit permission (field perms) */
#define FS_PERM_DIR   0x8000u   /* entri ini adalah direktori */
#define FS_PERM_READ  0x0004u   /* dapat dibaca */
#define FS_PERM_WRITE 0x0002u   /* dapat ditulis */
#define FS_PERM_EXEC  0x0001u   /* dapat dieksekusi */

typedef struct {
    char     name[FS_MAX_NAME]; /* 32: path/nama file, boleh mengandung '/' */
    uint32_t size;              /*  4: ukuran data aktual */
    uint8_t  used;              /*  1: slot aktif */
    uint8_t  tmpfs;             /*  1: 1 = tidak pernah disimpan ke disk */
    uint8_t  dirty;             /*  1: 1 = data berubah sejak sync terakhir */
    uint8_t  _pad;              /*  1: padding */
    uint16_t perms;             /*  2: FS_PERM_* flags */
    uint16_t _pad2;             /*  2: padding */
    uint32_t ctime;             /*  4: tick saat dibuat */
    uint32_t mtime;             /*  4: tick saat terakhir diubah */
    uint8_t  *data;             /*  8: pointer ke buffer data (dari heap); NULL jika slot kosong */
} FSFile;                       /* total ≈ 60 bytes, aligned to 64 */

void fs_init(void);
int  fs_write(const char *name, const char *data);
int  fs_write_bin(const char *name, const uint8_t *data, uint32_t size);
int  fs_write_tmp(const char *name, const uint8_t *data, uint32_t size); /* E3: write ke tmpfs */
int  fs_mkdir(const char *name);                /* buat entri direktori */
const uint8_t* fs_read_bin(const char *name, uint32_t *out_size);
const char*    fs_read(const char *name);
int  fs_delete(const char *name);
void fs_list(void (*print_fn)(const char*));
void fs_list_dir(const char *dir, void (*print_fn)(const char*)); /* list isi satu direktori */
int  fs_list_buf(char *buf, int bufsz);
int  fs_find_prefix(const char *prefix, char *out_name);
int  fs_flush(void);            /* E2: flush semua file dirty ke disk */
uint16_t fs_get_perms(const char *name);
int      fs_set_perms(const char *name, uint16_t perms);
uint32_t fs_get_mtime(const char *name);

/* F-R3: akses langsung tabel file untuk mfs4_init() */
FSFile  *fs_get_table(void);

#endif