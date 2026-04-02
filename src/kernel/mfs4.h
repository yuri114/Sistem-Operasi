/* mfs4.h — MFS4: inode-based metadata layer di atas MFS3
 *
 * MFS4 menambahkan:
 *   - Inode table (in-memory, MFS4_MAX_INODES entri)
 *   - Tipe entri: FILE, DIR, SYMLINK, HARDLINK
 *   - Symlink: mfs4_symlink("link", "target") — resolusi path transparan
 *   - Hardlink: mfs4_hardlink("alias", "original") — berbagi isi file yang sama
 *   - Stat: ukuran, tipe, ctime, mtime via mfs4_stat()
 *   - mfs4_listdir(dir) — list semua entri dalam direktori
 *
 * Kompatibilitas mundur: semua file MFS3 sudah ada otomatis terdaftar
 * di inode table saat mfs4_init() dipanggil.
 *
 * Layout inode:
 *   inode_id  : 0..MFS4_MAX_INODES-1
 *   type      : MFS4_TYPE_FILE | DIR | SYMLINK
 *   path      : path lengkap (e.g. "dir/file.txt")
 *   target    : untuk SYMLINK = path target; untuk HARDLINK = path asli
 *   mfs3_name : nama di MFS3 (untuk FILE dan HARDLINK)
 */
#ifndef MFS4_H
#define MFS4_H
#include <stdint.h>

#define MFS4_MAX_INODES  128
#define MFS4_PATH_LEN    64

/* Tipe inode */
#define MFS4_TYPE_NONE     0
#define MFS4_TYPE_FILE     1  /* regular file — data di MFS3          */
#define MFS4_TYPE_DIR      2  /* direktori — tanpa data; berisi inode anak */
#define MFS4_TYPE_SYMLINK  3  /* symlink — target di field target      */

typedef struct {
    uint8_t  type;                   /* MFS4_TYPE_*                    */
    uint8_t  used;
    uint16_t _pad;
    uint32_t inode_id;
    char     path[MFS4_PATH_LEN];    /* path lengkap (kunci unik)      */
    char     target[MFS4_PATH_LEN];  /* symlink target atau nama MFS3  */
    uint32_t ctime;
    uint32_t mtime;
} MFS4Inode;

/* Stat yang dikembalikan oleh mfs4_stat() */
typedef struct {
    uint8_t  type;     /* MFS4_TYPE_*                                  */
    uint32_t size;     /* ukuran data (0 untuk DIR)                    */
    uint32_t ctime;
    uint32_t mtime;
    uint16_t perms;    /* FS_PERM_* dari MFS3                          */
} MFS4Stat;

/* Inisialisasi: scan MFS3, daftarkan semua file/dir ke inode table.
 * Panggil setelah fs_init(). */
void mfs4_init(void);

/* Resolusi path: symlink di-follow, return nama final di MFS3 (atau NULL).
 * max_depth mencegah symlink loop (biasanya 8). */
const char *mfs4_resolve(const char *path, int max_depth);

/* Buat symlink.  Return 0 sukses, -1 gagal. */
int mfs4_symlink(const char *link_path, const char *target_path);

/* Buat hardlink — alias untuk file MFS3 yang sama. Return 0/-1. */
int mfs4_hardlink(const char *link_path, const char *original_path);

/* Buat direktori di inode table (MFS3 directory entry juga dibuat).
 * Return 0/-1. */
int mfs4_mkdir(const char *path);

/* Stat path (resolve symlink dulu). Return 0 sukses, -1 tidak ada. */
int mfs4_stat(const char *path, MFS4Stat *out);

/* List semua entry di sebuah direktori ke buf (newline-separated).
 * Return jumlah entry, -1 gagal. */
int mfs4_listdir(const char *dir, char *buf, int bufsz);

/* Hapus inode (dan file MFS3 jika TYPE_FILE/HARDLINK tanpa referensi lain).
 * Return 0/-1. */
int mfs4_unlink(const char *path);

/* Daftarkan satu file/dir MFS3 yang baru dibuat ke inode table.
 * Dipanggil otomatis setelah fs_write/fs_mkdir. */
int mfs4_register(const char *path, uint8_t type);

#endif /* MFS4_H */
