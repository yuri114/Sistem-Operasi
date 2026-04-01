#include "fs.h"
#include "ata.h"
#include "memory.h"

extern uint32_t get_ticks(void);
static uint32_t timer_get_ticks(void) { return get_ticks(); }

/* Tabel metadata — dialokasi dari heap di fs_init().
 * Data tiap file dialokasi terpisah (uint8_t *data) sehingga
 * array metadata hanya ~4KB meski ada 64 slot. */
static FSFile *files;

/* ===== Disk layout MFS3 (Primary Slave) =====
 * Sector 0                    : superblock (magic 'MFS3')
 * Sector 1 + i*129            : header file ke-i (512 byte)
 *   Offset di header:
 *     [  0- 31] name[32]
 *     [ 32- 35] size (uint32 LE)
 *     [     36] used
 *     [     37] tmpfs (1 = tidak pernah simpan ke disk)
 *     [ 38- 39] perms (uint16 LE)
 *     [ 40- 43] ctime (uint32 LE, timer tick)
 *     [ 44- 47] mtime (uint32 LE, timer tick)
 *     [ 48-511] reserved
 * Sector 2 + i*129 .. 129 + i*129 : data file ke-i (128 × 512 = 64KB)
 * Total: 1 + 64×129 = 8257 sektor ≈ 4.1MB
 */
#define FS_SECTORS_PER_FILE  (1 + FS_DATA_SECS)   /* 1 header + 128 data = 129 */
#define FS_MAGIC_B0 'M'
#define FS_MAGIC_B1 'F'
#define FS_MAGIC_B2 'S'
#define FS_MAGIC_B3 '3'

static uint32_t fs_hdr_sector(int i)             { return (uint32_t)(1 + (uint32_t)i * FS_SECTORS_PER_FILE); }
static uint32_t fs_data_sector(int i, int chunk) { return (uint32_t)(2 + (uint32_t)i * FS_SECTORS_PER_FILE + (uint32_t)chunk); }

/* Tulis header + data satu file ke disk (skip jika tmpfs atau tidak ada disk) */
static void fs_disk_save(int i) {
    uint8_t buf[512];
    int j;
    if (!ata_disk_present()) return;
    if (files[i].tmpfs) return;   /* E3: file tmpfs tidak pernah disimpan */

    /* --- header sector (512 byte) --- */
    for (j = 0; j < 512; j++) buf[j] = 0;
    for (j = 0; j < FS_MAX_NAME; j++) buf[j] = (uint8_t)files[i].name[j];
    buf[32] = (uint8_t)(files[i].size         & 0xFFu);
    buf[33] = (uint8_t)((files[i].size >>  8) & 0xFFu);
    buf[34] = (uint8_t)((files[i].size >> 16) & 0xFFu);
    buf[35] = (uint8_t)((files[i].size >> 24) & 0xFFu);
    buf[36] = files[i].used;
    buf[37] = files[i].tmpfs;
    buf[38] = (uint8_t)(files[i].perms        & 0xFFu);
    buf[39] = (uint8_t)((files[i].perms >> 8) & 0xFFu);
    buf[40] = (uint8_t)(files[i].ctime        & 0xFFu);
    buf[41] = (uint8_t)((files[i].ctime >>  8)& 0xFFu);
    buf[42] = (uint8_t)((files[i].ctime >> 16)& 0xFFu);
    buf[43] = (uint8_t)((files[i].ctime >> 24)& 0xFFu);
    buf[44] = (uint8_t)(files[i].mtime        & 0xFFu);
    buf[45] = (uint8_t)((files[i].mtime >>  8)& 0xFFu);
    buf[46] = (uint8_t)((files[i].mtime >> 16)& 0xFFu);
    buf[47] = (uint8_t)((files[i].mtime >> 24)& 0xFFu);
    ata_write_sector(fs_hdr_sector(i), buf);

    /* --- data sectors (FS_DATA_SECS × 512 = FS_MAX_DATA bytes) --- */
    if (!files[i].data) return;   /* direktori atau file kosong */
    int k;
    for (k = 0; k < FS_DATA_SECS; k++) {
        for (j = 0; j < 512; j++) {
            uint32_t off = (uint32_t)(k * 512 + j);
            buf[j] = (off < files[i].size) ? files[i].data[off] : 0;
        }
        ata_write_sector(fs_data_sector(i, k), buf);
    }
    files[i].dirty = 0;   /* sudah bersih */
}

/* Tulis magic MFS3 ke sektor 0 */
static void fs_disk_write_magic() {
    uint8_t buf[512];
    int j;
    for (j = 0; j < 512; j++) buf[j] = 0;
    buf[0] = FS_MAGIC_B0; buf[1] = FS_MAGIC_B1;
    buf[2] = FS_MAGIC_B2; buf[3] = FS_MAGIC_B3;
    ata_write_sector(0, buf);
}

/* Muat semua file dari disk (dipanggil saat boot) */
static void fs_disk_load() {
    uint8_t buf[512];
    int i, j, k;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (ata_read_sector(fs_hdr_sector(i), buf) != 0) continue;
        files[i].used = buf[36];
        if (!files[i].used) continue;
        for (j = 0; j < FS_MAX_NAME; j++) files[i].name[j] = (char)buf[j];
        files[i].size  = (uint32_t)buf[32] | ((uint32_t)buf[33]<<8)
                       | ((uint32_t)buf[34]<<16) | ((uint32_t)buf[35]<<24);
        files[i].tmpfs = buf[37];
        files[i].perms = (uint16_t)buf[38] | ((uint16_t)buf[39]<<8);
        files[i].ctime = (uint32_t)buf[40] | ((uint32_t)buf[41]<<8)
                       | ((uint32_t)buf[42]<<16) | ((uint32_t)buf[43]<<24);
        files[i].mtime = (uint32_t)buf[44] | ((uint32_t)buf[45]<<8)
                       | ((uint32_t)buf[46]<<16) | ((uint32_t)buf[47]<<24);
        files[i].dirty = 0;

        /* Direktori tidak menyimpan data */
        if (files[i].perms & FS_PERM_DIR) { files[i].data = 0; continue; }

        /* Alokasi buffer data dan load dari disk */
        files[i].data = (uint8_t*)malloc(FS_MAX_DATA);
        if (!files[i].data) { files[i].used = 0; continue; }
        for (k = 0; k < FS_DATA_SECS; k++) {
            if (ata_read_sector(fs_data_sector(i, k), buf) != 0) break;
            for (j = 0; j < 512; j++) {
                uint32_t off = (uint32_t)(k * 512 + j);
                if (off < FS_MAX_DATA) files[i].data[off] = buf[j];
            }
        }
    }
}

static int fs_strcmp(const char *a, const char *b) {
    int i=0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0; //tidak sama
        i++;
    }
    return a[i] == '\0' && b[i] == '\0'; //sama jika kedua string berakhir bersamaan
}
static void fs_strcpy(char *dest, const char *src, int max) {
    int i=0;
    while (src[i] && i< max - 1){
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void fs_init() {
    int i;
    files = (FSFile*)malloc((uint32_t)(FS_MAX_FILES * (int)sizeof(FSFile)));
    for (i = 0; i < FS_MAX_FILES; i++) {
        files[i].used  = 0;
        files[i].data  = 0;
        files[i].dirty = 0;
        files[i].tmpfs = 0;
        files[i].perms = 0;
        files[i].ctime = 0;
        files[i].mtime = 0;
    }
    if (!ata_disk_present()) return;

    uint8_t sb[512];
    if (ata_read_sector(0, sb) != 0) return;

    if (sb[0] == FS_MAGIC_B0 && sb[1] == FS_MAGIC_B1 &&
        sb[2] == FS_MAGIC_B2 && sb[3] == FS_MAGIC_B3) {
        fs_disk_load();
    } else {
        /* Disk baru atau format MFS2 lama — format ulang ke MFS3 */
        fs_disk_write_magic();
    }
}

/* Helper internal: alokasi atau dapatkan buffer data slot ke-i */
static uint8_t *fs_ensure_data(int i) {
    if (!files[i].data) {
        files[i].data = (uint8_t*)malloc(FS_MAX_DATA);
        if (!files[i].data) return 0;
        int j;
        for (j = 0; j < FS_MAX_DATA; j++) files[i].data[j] = 0;
    }
    return files[i].data;
}

int fs_write(const char *name, const char *data) {
    int i, len = 0;
    uint32_t tick = timer_get_ticks();
    while (data[len]) len++;
    if ((uint32_t)len >= FS_MAX_DATA) return 0;

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && !(files[i].perms & FS_PERM_DIR) && fs_strcmp(files[i].name, name)) {
            if (!fs_ensure_data(i)) return 0;
            fs_strcpy((char*)files[i].data, data, FS_MAX_DATA);
            files[i].size  = (uint32_t)(len + 1);
            files[i].mtime = tick;
            files[i].dirty = 1;
            fs_disk_save(i);
            return 1;
        }
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            fs_strcpy(files[i].name, name, FS_MAX_NAME);
            if (!fs_ensure_data(i)) return 0;
            fs_strcpy((char*)files[i].data, data, FS_MAX_DATA);
            files[i].size  = (uint32_t)(len + 1);
            files[i].used  = 1;
            files[i].tmpfs = 0;
            files[i].dirty = 1;
            files[i].perms = FS_PERM_READ | FS_PERM_WRITE;
            files[i].ctime = tick;
            files[i].mtime = tick;
            fs_disk_save(i);
            return 1;
        }
    }
    return 0;
}

const char* fs_read(const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && !(files[i].perms & FS_PERM_DIR) && fs_strcmp(files[i].name, name))
            return (const char*)files[i].data;
    }
    return 0;
}

int fs_delete(const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && fs_strcmp(files[i].name, name)) {
            files[i].used = 0;
            if (files[i].data) { free(files[i].data); files[i].data = 0; }
            fs_disk_save(i);   /* tulis used=0 ke disk */
            return 1;
        }
    }
    return 0;
}

void fs_list(void (*print_fn)(const char*)) {
    int i, count = 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) continue;
        if (files[i].perms & FS_PERM_DIR)
            print_fn("[DIR] ");
        print_fn(files[i].name);
        print_fn("\n");
        count++;
    }
    if (count == 0)
        print_fn("Tidak ada file\n");
}

/* List hanya entri yang berada langsung di dalam direktori 'dir'.
 * dir="" atau dir="/" → tampilkan entri root (tanpa '/' di tengah nama).
 * dir="docs/" → tampilkan entri seperti "docs/readme.txt" (hanya nama setelah "/"). */
void fs_list_dir(const char *dir, void (*print_fn)(const char*)) {
    int i, dirlen = 0, count = 0;
    while (dir[dirlen]) dirlen++;

    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) continue;
        const char *nm = files[i].name;
        int nmlen = 0; while (nm[nmlen]) nmlen++;

        if (dirlen == 0 || (dirlen == 1 && dir[0] == '/')) {
            /* root: nama tidak boleh mengandung '/' */
            int has_slash = 0, j;
            for (j = 0; nm[j]; j++) if (nm[j] == '/') { has_slash = 1; break; }
            if (!has_slash) {
                if (files[i].perms & FS_PERM_DIR) print_fn("[DIR] ");
                print_fn(nm); print_fn("\n"); count++;
            }
        } else {
            /* pastikan nama dimulai dengan dir */
            int match = 1, j;
            for (j = 0; j < dirlen; j++) {
                if (nm[j] != dir[j]) { match = 0; break; }
            }
            if (!match) continue;
            const char *tail = nm + dirlen;
            /* tail tidak boleh mengandung '/' (hanya satu level dalam) */
            int has_slash = 0;
            for (j = 0; tail[j]; j++) if (tail[j] == '/') { has_slash = 1; break; }
            if (!has_slash) {
                if (files[i].perms & FS_PERM_DIR) print_fn("[DIR] ");
                print_fn(tail); print_fn("\n"); count++;
            }
        }
    }
    if (count == 0) print_fn("Kosong\n");
}

int fs_list_buf(char *buf, int bufsz) {
    int pos = 0, cnt = 0;
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) continue;
        const char *nm = files[i].name;
        int j;
        for (j = 0; nm[j] && pos < bufsz - 2; j++) buf[pos++] = nm[j];
        if (pos < bufsz - 1) buf[pos++] = '\n';
        cnt++;
    }
    if (pos < bufsz) buf[pos] = '\0';
    return cnt;
}

int fs_find_prefix(const char *prefix, char *out_name) {
    int i, found = 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) continue;
        int j = 0, match = 1;
        while (prefix[j]) {
            if (files[i].name[j] != prefix[j]) { match = 0; break; }
            j++;
        }
        if (!match) continue;
        if (found) return 0;
        found = 1;
        j = 0;
        while (files[i].name[j] && j < FS_MAX_NAME - 1) {
            out_name[j] = files[i].name[j]; j++;
        }
        out_name[j] = '\0';
    }
    return found;
}

static void fs_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}

static int fs_write_bin_internal(const char *name, const uint8_t *data,
                                 uint32_t size, uint8_t is_tmpfs) {
    if (size > FS_MAX_DATA) return 0;
    uint32_t tick = timer_get_ticks();
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && !(files[i].perms & FS_PERM_DIR) && fs_strcmp(files[i].name, name)) {
            if (!fs_ensure_data(i)) return 0;
            fs_memcpy(files[i].data, data, size);
            files[i].size  = size;
            files[i].mtime = tick;
            files[i].dirty = 1;
            files[i].tmpfs = is_tmpfs;
            fs_disk_save(i);
            return 1;
        }
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            fs_strcpy(files[i].name, name, FS_MAX_NAME);
            if (!fs_ensure_data(i)) return 0;
            fs_memcpy(files[i].data, data, size);
            files[i].size  = size;
            files[i].used  = 1;
            files[i].tmpfs = is_tmpfs;
            files[i].dirty = 1;
            files[i].perms = FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXEC;
            files[i].ctime = tick;
            files[i].mtime = tick;
            fs_disk_save(i);
            return 1;
        }
    }
    return 0;
}

int fs_write_bin(const char *name, const uint8_t *data, uint32_t size) {
    return fs_write_bin_internal(name, data, size, 0);
}

/* E3: tulis ke tmpfs — tidak pernah disimpan ke disk */
int fs_write_tmp(const char *name, const uint8_t *data, uint32_t size) {
    return fs_write_bin_internal(name, data, size, 1);
}

int fs_mkdir(const char *name) {
    int i;
    uint32_t tick = timer_get_ticks();
    /* Cek duplikat */
    for (i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && fs_strcmp(files[i].name, name)) return 1; /* sudah ada */
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            fs_strcpy(files[i].name, name, FS_MAX_NAME);
            files[i].size  = 0;
            files[i].used  = 1;
            files[i].tmpfs = 0;
            files[i].dirty = 1;
            files[i].perms = FS_PERM_DIR | FS_PERM_READ | FS_PERM_WRITE;
            files[i].ctime = tick;
            files[i].mtime = tick;
            files[i].data  = 0;
            fs_disk_save(i);
            return 1;
        }
    }
    return 0;
}

const uint8_t* fs_read_bin(const char *name, uint32_t *out_size) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && !(files[i].perms & FS_PERM_DIR) && fs_strcmp(files[i].name, name)) {
            if (out_size) *out_size = files[i].size;
            return files[i].data;
        }
    }
    return 0;
}

/* E2: flush semua file yang dirty ke disk */
int fs_flush(void) {
    int i, count = 0;
    if (!ata_disk_present()) return 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && files[i].dirty && !files[i].tmpfs) {
            fs_disk_save(i);
            count++;
        }
    }
    return count;
}

uint16_t fs_get_perms(const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && fs_strcmp(files[i].name, name)) return files[i].perms;
    return 0;
}

int fs_set_perms(const char *name, uint16_t perms) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && fs_strcmp(files[i].name, name)) {
            files[i].perms = perms;
            files[i].dirty = 1;
            fs_disk_save(i);
            return 1;
        }
    }
    return 0;
}

uint32_t fs_get_mtime(const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && fs_strcmp(files[i].name, name)) return files[i].mtime;
    return 0;
}
