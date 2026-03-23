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
#define SYS_MSG_SEND 7
#define SYS_MSG_RECV 8
#define SYS_KILL      9
#define SYS_SEM_ALLOC  10
#define SYS_SEM_FREE   11
#define SYS_SEM_WAIT   12
#define SYS_SEM_POST   13
#define SYS_PIPE_OPEN  14
#define SYS_PIPE_WRITE 15
#define SYS_PIPE_READ  16
#define SYS_PIPE_CLOSE 17
#define SYS_PIPE_GETID 18

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

// syscall2: eax=num, ebx=arg1, edx=arg2 (3 argumen)
static inline int syscall2(int num, int arg1, int arg2) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "d"(arg2)
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

// ============================================================
// IPC — kirim dan terima pesan antar proses
// ============================================================

// Kirim pesan ke message queue kernel
// return 1 sukses, 0 jika queue penuh (maks 8 pesan)
static inline int msg_send(const char *msg) {
    return syscall1(SYS_MSG_SEND, (int)msg);
}

// Ambil satu pesan dari queue ke buffer buf (minimal 64 byte)
// return 1 jika ada pesan, 0 jika queue kosong
static inline int msg_recv(char *buf) {
    return syscall1(SYS_MSG_RECV, (int)buf);
}

// Kirim sinyal kill ke proses dengan id tertentu
// return 1 sukses, 0 gagal (id tidak valid / dilindungi)
static inline int kill(int id) {
    return syscall1(SYS_KILL, id);
}

// Alokasi semaphore baru dengan nilai awal (1=mutex tersedia, 0=terkunci)
// return: id semaphore untuk dipakai di sem_wait/sem_post/sem_free
static inline int sem_alloc(int initial_value) {
    return syscall1(SYS_SEM_ALLOC, initial_value);
}

// Bebaskan slot semaphore (panggil setelah selesai pakai)
static inline void sem_free(int id) {
    syscall1(SYS_SEM_FREE, id);
}

// Tunggu sampai semaphore tersedia lalu kunci (masuk critical section)
static inline int sem_wait(int id) {
    return syscall1(SYS_SEM_WAIT, id);
}

// Lepas kunci semaphore (keluar critical section)
static inline int sem_post(int id) {
    return syscall1(SYS_SEM_POST, id);
}

// ============================================================
// Pipe — anonymous channel untuk komunikasi antar proses
// ============================================================

// Buka (alokasi) pipe baru — return id pipe, atau -1 jika penuh
static inline int pipe_open() {
    return syscall0(SYS_PIPE_OPEN);
}

// Tulis string ke pipe (id = id pipe yang didapat dari pipe_open atau pipe_get_id)
static inline int pipe_write(int id, const char *msg) {
    return syscall2(SYS_PIPE_WRITE, id, (int)msg);
}

// Baca satu pesan dari pipe ke buf — return bytes dibaca, 0 jika kosong
static inline int pipe_read(int id, char *buf) {
    return syscall2(SYS_PIPE_READ, id, (int)buf);
}

// Tutup (bebaskan) pipe
static inline void pipe_close(int id) {
    syscall1(SYS_PIPE_CLOSE, id);
}

// Ambil pipe_id yang diwarisi dari shell (untuk program yang dijalankan via 'pipe' command)
// Return -1 jika tidak ada pipe yang di-assign
static inline int pipe_get_id() {
    return syscall0(SYS_PIPE_GETID);
}

#endif
