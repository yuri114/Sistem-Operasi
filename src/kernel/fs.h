#ifndef FS_H
#define FS_H
#include <stdint.h>

#define FS_MAX_FILES 16
#define FS_MAX_NAME 32
#define FS_MAX_DATA 256

typedef struct {
    char name[FS_MAX_NAME];
    char data[FS_MAX_DATA];
    uint8_t used;
} FSFile;

void fs_init();
int fs_write(const char *name, const char *data);
const char* fs_read(const char *name);
int fs_delete(const char *name);
void fs_list(void (*print_fn)(const char*));

#endif