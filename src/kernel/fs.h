#ifndef FS_H
#define FS_H
#include <stdint.h>

#define FS_MAX_FILES 16
#define FS_MAX_NAME 32
#define FS_MAX_DATA 8192

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

#endif