#include "fs.h"
#include "ata.h"
#include "memory.h"

/* Dialokasikan dari heap di fs_init() — tidak disimpan di BSS agar BSS
 * tidak meluap ke area VGA/BIOS (0xA0000+) dan merusak page tables. */
static FSFile *files;

/* ===== Disk layout (Primary Slave) =====
 * Sector 0              : superblock  (magic='MFS2' + reserved)
 * Sector 1+i*(1+DATA_S) : header file ke-i  (name[32], size[4], used[1], pad)
 * Sectors 2+i*(1+DATA_S) .. (DATA_S+1)+i*(1+DATA_S) : data file ke-i
 *   DATA_S = FS_DATA_SECS = 32  → 32 x 512 = 16384 bytes per file
 * Total: 1 + 32*(1+32) = 1057 sectors = 528 KB  (fits in 8 MB disk)
 */
#define FS_MAGIC_B0 'M'
#define FS_MAGIC_B1 'F'
#define FS_MAGIC_B2 'S'
#define FS_MAGIC_B3 '2'

static uint32_t fs_hdr_sector(int i)             { return (uint32_t)(1 + i * (1 + FS_DATA_SECS)); }
static uint32_t fs_data_sector(int i, int chunk) { return (uint32_t)(2 + i * (1 + FS_DATA_SECS) + chunk); }

/* Tulis satu file ke disk */
static void fs_disk_save(int i) {
    uint8_t buf[512];
    int j;
    if (!ata_disk_present()) return;

    /* --- header sector --- */
    for (j = 0; j < 512; j++) buf[j] = 0;
    for (j = 0; j < FS_MAX_NAME; j++) buf[j] = (uint8_t)files[i].name[j];
    buf[32] = (uint8_t)(files[i].size & 0xFFu);
    buf[33] = (uint8_t)((files[i].size >> 8)  & 0xFFu);
    buf[34] = (uint8_t)((files[i].size >> 16) & 0xFFu);
    buf[35] = (uint8_t)((files[i].size >> 24) & 0xFFu);
    buf[36] = files[i].used;
    ata_write_sector(fs_hdr_sector(i), buf);

    /* --- data sectors (FS_DATA_SECS x 512 = FS_MAX_DATA bytes) --- */
    int k;
    for (k = 0; k < FS_DATA_SECS; k++) {
        for (j = 0; j < 512; j++) {
            uint32_t off = (uint32_t)(k * 512 + j);
            buf[j] = (off < FS_MAX_DATA) ? files[i].data[off] : 0;
        }
        ata_write_sector(fs_data_sector(i, k), buf);
    }
}

/* Tulis magic ke sektor 0 */
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
        files[i].size = (uint32_t)buf[32] |
                        ((uint32_t)buf[33] << 8)  |
                        ((uint32_t)buf[34] << 16) |
                        ((uint32_t)buf[35] << 24);
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
    /* Alokasikan tabel file dari heap. Dipanggil setelah mem_init() — heap sudah siap. */
    files = (FSFile*)malloc((uint32_t)(FS_MAX_FILES * (int)sizeof(FSFile)));
    for (i = 0; i < FS_MAX_FILES; i++) {
        files[i].used = 0;
    }
    if (!ata_disk_present()) return; /* tidak ada disk sekunder, pakai RAM saja */

    uint8_t sb[512];
    if (ata_read_sector(0, sb) != 0) return; /* gagal baca */

    if (sb[0] == FS_MAGIC_B0 && sb[1] == FS_MAGIC_B1 &&
        sb[2] == FS_MAGIC_B2 && sb[3] == FS_MAGIC_B3) {
        /* Disk sudah diformat — muat semua file */
        fs_disk_load();
    } else {
        /* Disk baru — tulis magic agar boot berikutnya bisa load */
        fs_disk_write_magic();
    }
}

int fs_write(const char *name, const char * data) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && fs_strcmp(files[i].name, name)) {
            fs_strcpy(files[i].data, data, FS_MAX_DATA);
            fs_disk_save(i);
            return 1; //sukses
        }
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            fs_strcpy(files[i].name, name, FS_MAX_NAME);
            fs_strcpy(files[i].data, data, FS_MAX_DATA);
            files[i].used = 1;
            fs_disk_save(i);
            return 1; //sukses
        }
    }
    return 0; //gagal, tidak ada slot kosong
}

const char* fs_read(const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && fs_strcmp(files[i].name, name))
            return files[i].data; //kembalikan data jika nama cocok
    }
    return 0; //tidak ditemukan
}

int fs_delete(const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && fs_strcmp(files[i].name, name)) {
            files[i].used = 0;
            fs_disk_save(i); /* tulis used=0 ke disk */
            return 1; //sukses
        }
    }
    return 0; //tidak ditemukan
}
    
void fs_list(void (*print_fn)(const char*)) {
    int i, count = 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used) {
            print_fn(files[i].name);
            print_fn("\n");
            count++;
        }
    }
    if (count == 0) {
        print_fn("Tidak ada file\n");
    }
}

int fs_list_buf(char *buf, int bufsz) {
    int pos = 0, cnt = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) continue;
        const char *nm = files[i].name;
        for (int j = 0; nm[j] && pos < bufsz - 2; j++) buf[pos++] = nm[j];
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
        // cek apakah nama file dimulai dengan prefix
        int j = 0;
        int match = 1;
        while (prefix[j]) {
            if (files[i].name[j] != prefix[j]) { match = 0; break; }
            j++;
        }
        if (!match) continue;
        if (found) return 0; // lebih dari satu cocok = ambigu
        found = 1;
        j = 0;
        while (files[i].name[j] && j < FS_MAX_NAME - 1) {
            out_name[j] = files[i].name[j];
            j++;
        }
        out_name[j] = '\0';
    }
    return found;
}

static void fs_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

int fs_write_bin(const char *name, const uint8_t *data, uint32_t size) {
    if (size > FS_MAX_DATA) return 0; //gagal, data terlalu besar

    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && fs_strcmp(files[i].name, name)) {
            fs_memcpy(files[i].data, data, size);
            files[i].size = size;
            fs_disk_save(i);
            return 1; //sukses
        }
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            fs_strcpy(files[i].name, name, FS_MAX_NAME);
            fs_memcpy(files[i].data, data, size);
            files[i].size = size;
            files[i].used = 1;
            fs_disk_save(i);
            return 1; //sukses
        }
    }
    return 0; //gagal, tidak ada slot kosong
}

const uint8_t* fs_read_bin(const char *name, uint32_t *out_size) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && fs_strcmp(files[i].name, name)) {
            if (out_size) {
                *out_size = files[i].size; //kembalikan ukuran data jika pointer valid
            }
            return files[i].data; //kembalikan pointer ke data
        }
    }
    return 0; //tidak ditemukan
}
