/* mfs4.c — MFS4 inode layer */
#include "mfs4.h"
#include "fs.h"
#include "timer.h"
#include <stdint.h>

extern void print(const char *s);

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

    /* Scan semua file/dir MFS3 dan daftarkan */
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
        if (!nd || nd->type != MFS4_TYPE_SYMLINK) break;
        s_copy(resolved, nd->target, MFS4_PATH_LEN);
    }
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
