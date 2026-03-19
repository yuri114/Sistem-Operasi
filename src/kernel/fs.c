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
            print_fn(files[i].name); //panggil fungsi cetak untuk setiap nama file yang digunakan
            print_fn("\n");
            count++;
        }
    }
    if (count == 0) {
        print_fn("Tidak ada file\n");
    }
}

