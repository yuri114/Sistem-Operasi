/* mfs4.c — MFS4 inode layer */
#include "mfs4.h"
#include "fs.h"
#include "ata.h"
#include "timer.h"
#include <stdint.h>

extern void print(const char *s);

/* -----------------------------------------------------------------------
 * F-V1: Layout disk persistence MFS4
 *   LBA 513       : metadata — magic[4] + next_inode_id[4]
 *   LBA 514–549   : inode table — 128 × sizeof(MFS4Inode) = 18432 bytes
 *                   = 36 sektor (18432 / 512 = 36 tepat)
 * ----------------------------------------------------------------------- */
#define MFS4_MAGIC0     'M'
#define MFS4_MAGIC1     'F'
#define MFS4_MAGIC2     'S'
#define MFS4_MAGIC3     '4'
#define MFS4_LBA_META   513u
#define MFS4_LBA_DATA   514u
/* sizeof(MFS4Inode) = 144 bytes; 128 * 144 = 18432 B = 36 sektor */
#define MFS4_INODE_BYTES  ((uint32_t)(MFS4_MAX_INODES * sizeof(MFS4Inode)))
#define MFS4_INODE_SECTS  ((MFS4_INODE_BYTES + 511u) / 512u)

/* ------------------------------------------------------------------ */
static MFS4Inode inodes[MFS4_MAX_INODES];
static uint32_t  next_inode_id = 1;

/* ---- string helpers ---- */
static int s_len(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}
static int s_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == '\0' && b[i] == '\0';
}
static void s_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* Kembalikan nama direktori dari path (bagian sebelum '/' terakhir).
 * Hasil ditulis ke out_dir (max dir_sz byte).
 * Jika tidak ada '/', out_dir = "". */
static void path_dirname(const char *path, char *out_dir, int dir_sz) {
    int last = -1, i = 0;
    while (path[i]) { if (path[i] == '/') last = i; i++; }
    if (last < 0) { out_dir[0] = '\0'; return; }
    int n = last < dir_sz - 1 ? last : dir_sz - 1;
    for (i = 0; i < n; i++) out_dir[i] = path[i];
    out_dir[n] = '\0';
}

/* Cari inode berdasarkan path, return pointer atau NULL. */
static MFS4Inode *find_inode(const char *path) {
    int i;
    for (i = 0; i < MFS4_MAX_INODES; i++)
        if (inodes[i].used && s_eq(inodes[i].path, path)) return &inodes[i];
    return 0;
}

/* Alokasi slot inode kosong. */
static MFS4Inode *alloc_inode(void) {
    int i;
    for (i = 0; i < MFS4_MAX_INODES; i++)
        if (!inodes[i].used) return &inodes[i];
    return 0;
}

/* ------------------------------------------------------------------ */
void mfs4_init(void)
{
    int i;
    for (i = 0; i < MFS4_MAX_INODES; i++) inodes[i].used = 0;
    next_inode_id = 1;

    /* F-V1: coba baca dari disk dulu — jika berhasil, data persisten digunakan */
    if (mfs4_load() == 0) return;

    /* Disk kosong / belum pernah flush: scan semua file/dir MFS3 dan daftarkan */
    extern FSFile *fs_get_table(void);  /* akses langsung tabel MFS3 */
    FSFile *tbl = fs_get_table();
    if (!tbl) return;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!tbl[i].used) continue;
        uint8_t t = (tbl[i].perms & FS_PERM_DIR) ? MFS4_TYPE_DIR : MFS4_TYPE_FILE;
        MFS4Inode *nd = alloc_inode();
        if (!nd) break;
        nd->used      = 1;
        nd->type      = t;
        nd->inode_id  = next_inode_id++;
        s_copy(nd->path,   tbl[i].name, MFS4_PATH_LEN);
        s_copy(nd->target, tbl[i].name, MFS4_PATH_LEN);  /* target = diri sendiri */
        nd->ctime     = tbl[i].ctime;
        nd->mtime     = tbl[i].mtime;
    }
}

/* ------------------------------------------------------------------ */
const char *mfs4_resolve(const char *path, int max_depth)
{
    static char resolved[MFS4_PATH_LEN];
    s_copy(resolved, path, MFS4_PATH_LEN);
    int d;
    for (d = 0; d < max_depth; d++) {
        MFS4Inode *nd = find_inode(resolved);
        if (!nd || nd->type != MFS4_TYPE_SYMLINK) return resolved;
        s_copy(resolved, nd->target, MFS4_PATH_LEN);
    }
    /* Masih symlink setelah max_depth iterasi → cycle (A->B->A) atau chain
     * terlalu panjang. Kembalikan "" agar find_inode() di pemanggil gagal
     * (-1) secara jelas, bukan path symlink yang belum ter-resolve. */
    resolved[0] = '\0';
    return resolved;
}

/* ------------------------------------------------------------------ */
int mfs4_symlink(const char *link_path, const char *target_path)
{
    if (!link_path || !target_path) return -1;
    if (find_inode(link_path)) return -1;  /* sudah ada */
    MFS4Inode *nd = alloc_inode();
    if (!nd) return -1;
    nd->used     = 1;
    nd->type     = MFS4_TYPE_SYMLINK;
    nd->inode_id = next_inode_id++;
    s_copy(nd->path,   link_path,   MFS4_PATH_LEN);
    s_copy(nd->target, target_path, MFS4_PATH_LEN);
    nd->ctime = nd->mtime = (uint32_t)get_ticks();
    return 0;
}

/* ------------------------------------------------------------------ */
int mfs4_hardlink(const char *link_path, const char *original_path)
{
    if (!link_path || !original_path) return -1;
    if (find_inode(link_path)) return -1;  /* sudah ada */
    /* Original harus ada dan bertipe FILE */
    MFS4Inode *orig = find_inode(original_path);
    if (!orig || orig->type != MFS4_TYPE_FILE) return -1;

    MFS4Inode *nd = alloc_inode();
    if (!nd) return -1;
    nd->used     = 1;
    nd->type     = MFS4_TYPE_FILE;   /* hardlink = file biasa dgn target berbeda */
    nd->inode_id = orig->inode_id;   /* sama-sama share inode_id asli */
    s_copy(nd->path,   link_path,        MFS4_PATH_LEN);
    s_copy(nd->target, orig->target,     MFS4_PATH_LEN);  /* target MFS3 sama */
    nd->ctime = nd->mtime = (uint32_t)get_ticks();
    return 0;
}

/* ------------------------------------------------------------------ */
int mfs4_mkdir(const char *path)
{
    if (!path || path[0] == '\0') return -1;
    if (find_inode(path)) return -1;  /* sudah ada */
    /* Buat entri direktori di MFS3 juga */
    fs_mkdir(path);
    MFS4Inode *nd = alloc_inode();
    if (!nd) return -1;
    nd->used     = 1;
    nd->type     = MFS4_TYPE_DIR;
    nd->inode_id = next_inode_id++;
    s_copy(nd->path,   path, MFS4_PATH_LEN);
    s_copy(nd->target, path, MFS4_PATH_LEN);
    nd->ctime = nd->mtime = (uint32_t)get_ticks();
    return 0;
}

/* ------------------------------------------------------------------ */
int mfs4_stat(const char *path, MFS4Stat *out)
{
    if (!path || !out) return -1;
    const char *real = mfs4_resolve(path, 8);
    MFS4Inode *nd = find_inode(real);
    if (!nd) return -1;

    out->type  = nd->type;
    out->ctime = nd->ctime;
    out->mtime = nd->mtime;
    out->perms = 0;
    out->size  = 0;

    if (nd->type == MFS4_TYPE_FILE) {
        uint32_t sz = 0;
        fs_read_bin(nd->target, &sz);
        out->size  = sz;
        out->perms = fs_get_perms(nd->target);
    } else if (nd->type == MFS4_TYPE_DIR) {
        out->perms = FS_PERM_DIR | FS_PERM_READ;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
int mfs4_listdir(const char *dir, char *buf, int bufsz)
{
    int i, count = 0, pos = 0;
    int dir_len = s_len(dir);

    for (i = 0; i < MFS4_MAX_INODES; i++) {
        if (!inodes[i].used) continue;
        const char *p = inodes[i].path;
        int plen = s_len(p);

        /* Cek apakah p adalah direct child dari dir:
         *   dir="" → semua top-level (tidak ada '/')
         *   dir="foo" → mulai "foo/", tidak ada '/' setelah itu */
        int is_child = 0;
        if (dir_len == 0) {
            /* top-level: path tidak boleh mengandung '/' */
            int j, found_slash = 0;
            for (j = 0; j < plen; j++) if (p[j] == '/') { found_slash = 1; break; }
            is_child = !found_slash;
        } else {
            /* harus diawali "dir/" dan tidak ada '/' setelahnya */
            if (plen > dir_len + 1 && p[dir_len] == '/') {
                int j, match = 1;
                for (j = 0; j < dir_len; j++) if (p[j] != dir[j]) { match = 0; break; }
                if (match) {
                    /* periksa tidak ada '/' lagi setelah dir/ */
                    int k, extra_slash = 0;
                    for (k = dir_len + 1; k < plen; k++) if (p[k] == '/') { extra_slash = 1; break; }
                    is_child = !extra_slash;
                }
            }
        }
        if (!is_child) continue;

        /* Ambil bagian nama saja (setelah '/' terakhir) */
        const char *name = p;
        int j;
        for (j = 0; j < plen; j++) if (p[j] == '/') name = p + j + 1;

        /* Tulis ke buf */
        int nlen = s_len(name);
        if (pos + nlen + 2 >= bufsz) break;
        for (j = 0; j < nlen; j++) buf[pos++] = name[j];
        if (inodes[i].type == MFS4_TYPE_DIR) buf[pos++] = '/';
        buf[pos++] = '\n';
        count++;
    }
    if (pos < bufsz) buf[pos] = '\0';
    return count;
}

/* ------------------------------------------------------------------ */
int mfs4_unlink(const char *path)
{
    if (!path) return -1;
    MFS4Inode *nd = find_inode(path);
    if (!nd) return -1;

    if (nd->type == MFS4_TYPE_FILE) {
        /* Cek apakah ada hardlink lain yang share inode_id yang sama */
        int i, refs = 0;
        for (i = 0; i < MFS4_MAX_INODES; i++)
            if (inodes[i].used && inodes[i].type == MFS4_TYPE_FILE &&
                inodes[i].inode_id == nd->inode_id)
                refs++;
        if (refs <= 1) fs_delete(nd->target);  /* hapus dari MFS3 juga */
    }
    nd->used = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
int mfs4_register(const char *path, uint8_t type)
{
    if (!path || !path[0]) return -1;
    if (find_inode(path)) return 0;  /* sudah terdaftar */
    MFS4Inode *nd = alloc_inode();
    if (!nd) return -1;
    nd->used     = 1;
    nd->type     = type;
    nd->inode_id = next_inode_id++;
    s_copy(nd->path,   path, MFS4_PATH_LEN);
    s_copy(nd->target, path, MFS4_PATH_LEN);
    nd->ctime = nd->mtime = (uint32_t)get_ticks();
    return 0;
}

/* Ekspos tabel inode untuk fs.c */
FSFile *fs_get_table(void);

/* -----------------------------------------------------------------------
 * F-V1: mfs4_flush — tulis inode table ke disk
 * ----------------------------------------------------------------------- */
void mfs4_flush(void)
{
    int i;
    uint8_t buf[512];

    if (!ata_disk_present()) return;

    /* Sektor metadata: magic + next_inode_id */
    for (i = 0; i < 512; i++) buf[i] = 0;
    buf[0] = MFS4_MAGIC0; buf[1] = MFS4_MAGIC1;
    buf[2] = MFS4_MAGIC2; buf[3] = MFS4_MAGIC3;
    buf[4] = (uint8_t)(next_inode_id);
    buf[5] = (uint8_t)(next_inode_id >> 8);
    buf[6] = (uint8_t)(next_inode_id >> 16);
    buf[7] = (uint8_t)(next_inode_id >> 24);
    ata_write_sector(MFS4_LBA_META, buf);

    /* Sektor data: inode table mentah */
    const uint8_t *raw = (const uint8_t *)inodes;
    uint32_t total  = MFS4_INODE_BYTES;
    uint32_t offset = 0;
    uint32_t lba    = MFS4_LBA_DATA;

    while (offset < total) {
        uint32_t n = total - offset;
        if (n > 512u) n = 512u;
        for (i = 0; i < (int)n; i++)    buf[i] = raw[offset + i];
        for (i = (int)n; i < 512; i++) buf[i] = 0;
        ata_write_sector(lba, buf);
        offset += 512u;
        lba++;
    }
}

/* -----------------------------------------------------------------------
 * F-V1: mfs4_load — baca inode table dari disk
 * Return 0 jika berhasil (magic cocok), -1 jika tidak ada data.
 * ----------------------------------------------------------------------- */
int mfs4_load(void)
{
    int i;
    uint8_t buf[512];

    if (!ata_disk_present()) return -1;

    if (ata_read_sector(MFS4_LBA_META, buf) != 0) return -1;
    if (buf[0] != MFS4_MAGIC0 || buf[1] != MFS4_MAGIC1 ||
        buf[2] != MFS4_MAGIC2 || buf[3] != MFS4_MAGIC3) return -1;

    next_inode_id = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                   ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);

    uint8_t *raw    = (uint8_t *)inodes;
    uint32_t total  = MFS4_INODE_BYTES;
    uint32_t offset = 0;
    uint32_t lba    = MFS4_LBA_DATA;

    while (offset < total) {
        if (ata_read_sector(lba, buf) != 0) return -1;
        uint32_t n = total - offset;
        if (n > 512u) n = 512u;
        for (i = 0; i < (int)n; i++) raw[offset + i] = buf[i];
        offset += 512u;
        lba++;
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * F-V3: mfs4_rename — ganti nama path sebuah inode
 * Untuk inode FILE: rename juga di MFS3 (via fs_rename).
 * Hardlink lain yang share inode_id tetap dengan path aslinya.
 * ----------------------------------------------------------------------- */
int mfs4_rename(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path) return -1;
    MFS4Inode *nd = find_inode(old_path);
    if (!nd) return -1;
    if (find_inode(new_path)) return -1;  /* tujuan sudah ada */

    /* Untuk FILE: rename entry di MFS3 terlebih dulu */
    if (nd->type == MFS4_TYPE_FILE) {
        if (fs_rename(nd->target, new_path) != 0) return -1;
        /* Propagasikan target baru ke semua hardlink yang share inode_id
         * (termasuk nd sendiri) — data MFS3 sudah berpindah nama, jadi
         * setiap inode yang merujuk ke nama lama kini akan menjadi broken
         * link kalau target-nya tidak ikut diupdate. */
        int i;
        for (i = 0; i < MFS4_MAX_INODES; i++) {
            if (inodes[i].used && inodes[i].type == MFS4_TYPE_FILE &&
                inodes[i].inode_id == nd->inode_id) {
                s_copy(inodes[i].target, new_path, MFS4_PATH_LEN);
            }
        }
    }

    s_copy(nd->path, new_path, MFS4_PATH_LEN);
    nd->mtime = (uint32_t)get_ticks();
    return 0;
}
