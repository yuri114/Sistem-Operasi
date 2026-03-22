#ifndef LIB_H
#define LIB_H

// ============================================================
// Nomor syscall (harus sama dengan kernel/syscall.h)
// ============================================================
#define SYS_PRINT    0
#define SYS_GETKEY   1
#define SYS_EXIT     2
#define SYS_ALLOC    3
#define SYS_FREE     4
#define SYS_FS_READ  5
#define SYS_FS_WRITE 6

// ============================================================
// syscall — memanggil kernel lewat int 0x80
//   eax = nomor syscall
//   ebx = argumen (pointer, nilai, dll)
//   return value ada di eax setelah int 0x80
// ============================================================
static inline int syscall1(int num, int arg) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg)
    );
    return ret;
}

static inline int syscall0(int num) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "ebx"
    );
    return ret;
}

// ============================================================
// Fungsi-fungsi untuk program user
// ============================================================

// Cetak string ke layar
static inline void print(const char *msg) {
    syscall1(SYS_PRINT, (int)msg);
}

// Tunggu input keyboard, kembalikan karakter yang ditekan
static inline char getkey() {
    return (char)syscall0(SYS_GETKEY);
}

// Keluar dari program
static inline void exit() {
    syscall0(SYS_EXIT);
    while (1); // tidak pernah sampai sini, tapi mencegah compiler warning
}

// Alokasikan memori sejumlah size byte, kembalikan pointer (atau 0 jika gagal)
static inline void* malloc(int size) {
    return (void*)syscall1(SYS_ALLOC, size);
}

// Bebaskan memori yang sebelumnya dialokasikan dengan malloc
static inline void free(void *ptr) {
    syscall1(SYS_FREE, (int)ptr);
}

// ============================================================
// Utilitas string
// ============================================================

// Hitung panjang string
static inline int strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

// Konversi integer tak bertanda ke string desimal, tulis ke buf
static inline void itoa(unsigned int n, char *buf) {
    if (n == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[12];
    int i = 0;
    while (n > 0) {
        tmp[i++] = '0' + (n % 10);
        n /= 10;
    }
    int j;
    for (j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

// Salin string dari src ke dst
static inline void strcpy(char *dst, const char *src) {
    int i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

// Bandingkan dua string: kembalikan 1 jika sama, 0 jika berbeda
static inline int strcmp(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

// Cetak integer ke layar
static inline void print_int(unsigned int n) {
    char buf[12];
    itoa(n, buf);
    print(buf);
}

// ============================================================
// File I/O — akses filesystem kernel lewat syscall
// ============================================================

// Baca file: kembalikan pointer ke isi file (string), atau 0 jika tidak ditemukan
static inline const char* fs_read(const char *name) {
    return (const char*)syscall1(SYS_FS_READ, (int)name);
}

// Tulis file: name = nama file, data = isi file (string)
// Kembalikan 1 jika sukses, 0 jika gagal (filesystem penuh)
static inline int fs_write(const char *name, const char *data) {
    // kirim dua argumen lewat satu pointer ke array of pointer
    const char *args[2];
    args[0] = name;
    args[1] = data;
    return syscall1(SYS_FS_WRITE, (int)args);
}

#endif
