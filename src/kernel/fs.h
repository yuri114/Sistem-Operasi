#ifndef FS_H
#define FS_H
#include <stdint.h>

#define FS_MAX_FILES 32
#define FS_MAX_NAME  32
#define FS_MAX_DATA  16384   /* 32 sektor x 512 bytes per file — 2x lebih banyak file, maks 16KB/file */
#define FS_DATA_SECS (FS_MAX_DATA / 512)  /* jumlah sektor data per file */

typedef struct {
    char name[FS_MAX_NAME];
    uint8_t data[FS_MAX_DATA];
    uint32_t size;
    uint8_t used;

} FSFile;

void fs_init();
int fs_write(const char *name, const char *data);
int fs_write_bin(const char *name, const uint8_t *data, uint32_t size);
const uint8_t* fs_read_bin(const char *name, uint32_t *out_size);
const char* fs_read(const char *name);
int fs_delete(const char *name);
void fs_list(void (*print_fn)(const char*));
int  fs_list_buf(char *buf, int bufsz);
// Cari satu file yang namanya diawali prefix; 1=ditemukan unik, 0=tidak ada/ambigu
int fs_find_prefix(const char *prefix, char *out_name);

#endif