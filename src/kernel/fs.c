#include "fs.h"

static FSFile files[FS_MAX_FILES];

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
    for (i = 0; i < FS_MAX_FILES; i++) {
        files[i].used = 0;
    }
}

int fs_write(const char *name, const char * data) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && fs_strcmp(files[i].name, name)) {
            fs_strcpy(files[i].data, data, FS_MAX_DATA);
            return 1; //sukses
        }
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            fs_strcpy(files[i].name, name, FS_MAX_NAME);
            fs_strcpy(files[i].data, data, FS_MAX_DATA);
            files[i].used = 1;
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
            return 1; //sukses
        }
    }
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            fs_strcpy(files[i].name, name, FS_MAX_NAME);
            fs_memcpy(files[i].data, data, size);
            files[i].size = size;
            files[i].used = 1;
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
