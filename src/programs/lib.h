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
#define SYS_DEV_WRITE  19
#define SYS_DEV_READ   20
#define SYS_DEV_IOCTL  21

// Syscall grafis (Phase 24)
#define SYS_DRAW_PIXEL  22
#define SYS_FILL_SCREEN 23
#define SYS_FILL_RECT   24
#define SYS_DRAW_LINE   25
#define SYS_CLR_SCREEN  26

// Syscall manajemen proses (Phase B/C)
#define SYS_GETPID 27  // kembalikan pid task saat ini
#define SYS_YIELD  28  // lepas sisa slot CPU
#define SYS_SLEEP  29  // tidur N milidetik
#define SYS_EXEC   30  // jalankan program dari FS: arg=nama

// Syscall grafis teks + mouse (Phase Mouse+Font)
#define SYS_DRAW_CHAR  31  // gambar 1 karakter 8x8: ebx=x|(y<<16), edx=c|(fg<<8)|(bg<<16)
#define SYS_DRAW_STR   32  // gambar string 8x8: ebx=ptr GfxStr
#define SYS_MOUSE_GET  33  // baca posisi+tombol mouse: ebx=ptr MouseState (output)

// Window Manager syscalls
#define SYS_WIN_CREATE  34  // buat window: ebx=ptr WinCreateArgs → return id
#define SYS_WIN_DESTROY 35  // tutup window: ebx=id
#define SYS_WIN_DRAW    36  // gambar teks di konten: ebx=ptr WinDrawArgs
#define SYS_WIN_CLEAR   37  // bersihkan konten: ebx=id, edx=warna_bg
#define SYS_WIN_EVENT   38  // poll event: ebx=id → return WIN_EVENT_* (encode char/btn di byte atas)
#define SYS_WIN_BTN_ADD   39  // tambah tombol ke window: ebx=ptr WinBtnArgs → return btn_idx
#define SYS_WIN_CLICK_POS 40  // koordinat klik konten terakhir: ebx=id, edx=ptr int[2]
#define SYS_WIN_DRAW_PIXEL 41 // gambar piksel di konten window: ebx=id, edx=x|(y<<12)|(color<<24)
#define SYS_WIN_FILL_RECT  42 // isi persegi di konten window: ebx=id, edx=ptr WinFillArgs
#define SYS_WIN_MOUSE_REL  43 // posisi mouse relatif konten: ebx=id, edx=ptr int[3] → [rel_x, rel_y, btn]
#define SYS_WIN_MINIMIZE   44 // minimize window: ebx=id
#define SYS_WIN_RESTORE    45 // restore window dari minimized: ebx=id
#define SYS_FS_LIST        46 // list nama file ke buffer: ebx=ptr, edx=bufsz → return count
#define SYS_FS_DELETE      47 // hapus file: ebx=ptr nama → return 1/0

// Event type konstanta
#define WIN_EVENT_NONE   0
#define WIN_EVENT_CLOSE  1
#define WIN_EVENT_CLICK  2
#define WIN_EVENT_KEY    3   /* ada input keyboard: (char << 8) | WIN_EVENT_KEY */
#define WIN_EVENT_BTN    4   /* tombol diklik: (btn_idx << 8) | WIN_EVENT_BTN */

// Macro decode event composite
#define WIN_EV_TYPE(ev)  ((ev) & 0xFF)
#define WIN_EV_CHAR(ev)  ((char)(((ev) >> 8) & 0xFF))
#define WIN_EV_BTN(ev)   (((ev) >> 8) & 0xFF)

// Device ID (harus sama dengan device.h)
#define DEV_VGA  0
#define DEV_KBD  1

// VGA ioctl commands
#define VGA_IOCTL_SET_COLOR  0  // arg = byte warna (fg | bg<<4)
#define VGA_IOCTL_CLEAR      1  // clear screen
#define VGA_IOCTL_GET_COLOR  2  // return warna saat ini

// Keyboard ioctl commands
#define KBD_IOCTL_FLUSH      0  // kosongkan buffer keyboard

// Warna Mode 13h palette (0-15 = CGA/EGA standard)
#define GFX_BLACK     0
#define GFX_BLUE      1
#define GFX_GREEN     2
#define GFX_CYAN      3
#define GFX_RED       4
#define GFX_MAGENTA   5
#define GFX_BROWN     6
#define GFX_LGRAY     7
#define GFX_DGRAY     8
#define GFX_LBLUE     9
#define GFX_LGREEN   10
#define GFX_LCYAN    11
#define GFX_LRED     12
#define GFX_LMAGENTA 13
#define GFX_YELLOW   14
#define GFX_WHITE    15

// Struct argumen syscall grafis — layout harus cocok dengan GfxRect/GfxLine di graphics.h
typedef struct { int x, y, w, h; unsigned char color; } GfxRect;
typedef struct { int x1, y1, x2, y2; unsigned char color; } GfxLine;

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

// ============================================================
// Device Driver — akses hardware lewat abstraksi driver
// ============================================================

// Tulis string ke device (misal: print ke layar via DEV_VGA)
static inline int dev_write(int dev_id, const char *msg) {
    return syscall2(SYS_DEV_WRITE, dev_id, (int)msg);
}

// Baca satu karakter dari device ke buf (misal: keyboard via DEV_KBD)
// Return 1 jika ada karakter, 0 jika kosong
static inline int dev_read(int dev_id, char *buf) {
    return syscall2(SYS_DEV_READ, dev_id, (int)buf);
}

// Kontrol khusus device (cmd dan arg dikemas dalam edx: cmd<<16 | arg)
static inline int dev_ioctl(int dev_id, int cmd, int arg) {
    return syscall2(SYS_DEV_IOCTL, dev_id, (cmd << 16) | (arg & 0xFFFF));
}

// ============================================================
// Grafis Mode 13h — akses framebuffer 320x200 via syscall
// ============================================================

// Gambar satu piksel di (x, y) dengan warna 0-255
// edx = (y << 16) | color  → y mendukung 0–479, color 0–255
static inline void gfx_pixel(int x, int y, unsigned char color) {
    syscall2(SYS_DRAW_PIXEL, x, (y << 16) | (int)color);
}

// Isi seluruh layar dengan satu warna
static inline void gfx_fill(unsigned char color) {
    syscall1(SYS_FILL_SCREEN, (int)color);
}

// Bersihkan layar dan reset posisi kursor teks ke (0,0)
static inline void gfx_clear() {
    syscall0(SYS_CLR_SCREEN);
}

// ============================================================
// Manajemen proses
// ============================================================

// Kembalikan id proses (task) saat ini
static inline int getpid() {
    return syscall0(SYS_GETPID);
}

// Lepas sisa slot CPU ke task lain (kooperatif yield)
static inline void yield() {
    syscall0(SYS_YIELD);
}

// Tidur selama ms milidetik
static inline void sleep_ms(unsigned int ms) {
    syscall1(SYS_SLEEP, (int)ms);
}

// Muat dan jalankan program dari filesystem (nama=string null-terminated)
// Return: task_id jika sukses, -1 jika gagal
static inline int exec(const char *name) {
    return syscall1(SYS_EXEC, (int)name);
}

// Gambar persegi panjang terisi menggunakan struct pointer
// (hindari variable length array di stack)
static inline void gfx_rect_s(GfxRect *r) {
    syscall1(SYS_FILL_RECT, (int)r);
}

// Gambar garis lurus menggunakan struct pointer
static inline void gfx_line_s(GfxLine *l) {
    syscall1(SYS_DRAW_LINE, (int)l);
}

// Shorthand: gambar persegi panjang tanpa deklarasi struct terpisah
static inline void gfx_rect(int x, int y, int w, int h, unsigned char color) {
    GfxRect r = {x, y, w, h, color};
    syscall1(SYS_FILL_RECT, (int)&r);
}

// Shorthand: gambar garis tanpa deklarasi struct terpisah
static inline void gfx_line(int x1, int y1, int x2, int y2, unsigned char color) {
    GfxLine l = {x1, y1, x2, y2, color};
    syscall1(SYS_DRAW_LINE, (int)&l);
}

// ============================================================
// Teks grafis (font 8x8) dan Mouse
// ============================================================

// Struct untuk menggambar string — layout harus cocok dengan GfxStr di graphics.h
typedef struct { int x, y; const char *s; unsigned char fg, bg; } GfxStr;

// State mouse — layout harus cocok dengan MouseState di mouse.h
typedef struct { int x, y; unsigned char buttons; } MouseState;

// Gambar satu karakter 8x8 di framebuffer pada (x,y)
static inline void gfx_char(int x, int y, char c, unsigned char fg, unsigned char bg) {
    syscall2(SYS_DRAW_CHAR, x | (y << 16),
             (int)((unsigned char)c | ((unsigned int)fg << 8) | ((unsigned int)bg << 16)));
}

// Gambar string null-terminated dengan font 8x8 di (x,y)
static inline void gfx_str(int x, int y, const char *s, unsigned char fg, unsigned char bg) {
    GfxStr g = {x, y, s, fg, bg};
    syscall1(SYS_DRAW_STR, (int)&g);
}

// Baca posisi dan status tombol mouse ke *ms
static inline void mouse_get(MouseState *ms) {
    syscall1(SYS_MOUSE_GET, (int)ms);
}

// ============================================================
// Window Manager
// ============================================================

// Struct argumen window — layout harus cocok dengan window.h di kernel
typedef struct { int x, y, w, h; const char *title; } WinCreateArgs;
typedef struct { int id, x, y; const char *s; unsigned char fg, bg; } WinDrawArgs;
typedef struct { int id, x, y, w, h; const char *label; } WinBtnArgs;

// Buat window baru, kembalikan id (0-15) atau -1 jika gagal
static inline int win_create(int x, int y, int w, int h, const char *title) {
    WinCreateArgs a = {x, y, w, h, title};
    return syscall1(SYS_WIN_CREATE, (int)&a);
}

// Tutup dan hapus window
static inline void win_destroy(int id) {
    syscall1(SYS_WIN_DESTROY, id);
}

// Gambar teks di area konten window pada offset piksel (px, py)
static inline void win_draw(int id, int px, int py, const char *s,
                             unsigned char fg, unsigned char bg) {
    WinDrawArgs d = {id, px, py, s, fg, bg};
    syscall1(SYS_WIN_DRAW, (int)&d);
}

// Bersihkan area konten window dengan warna bg
static inline void win_clear(int id, unsigned char bg) {
    syscall2(SYS_WIN_CLEAR, id, (int)bg);
}

// Poll event: kembalikan WIN_EVENT_* gabungan
// Untuk WIN_EVENT_KEY: gunakan WIN_EV_CHAR(ev) untuk dapat karakter
// Untuk WIN_EVENT_BTN: gunakan WIN_EV_BTN(ev) untuk dapat indeks tombol
static inline int win_poll(int id) {
    return syscall1(SYS_WIN_EVENT, id);
}

// Tambah tombol ke window — kembalikan idx tombol (0-7) atau -1 jika gagal
static inline int win_btn_add(int id, int x, int y, int w, int h, const char *label) {
    WinBtnArgs a = {id, x, y, w, h, label};
    return syscall1(SYS_WIN_BTN_ADD, (int)&a);
}

// Ambil koordinat klik konten terakhir (piksel relatif area konten window)
// Panggil setelah win_poll() return WIN_EVENT_CLICK
static inline void win_click_pos(int id, int *out_x, int *out_y) {
    int pos[2];
    syscall2(SYS_WIN_CLICK_POS, id, (int)pos);
    *out_x = pos[0];
    *out_y = pos[1];
}

// Gambar piksel warna di koordinat area konten window
// cx, cy: piksel relatif area konten (0,0 = pojok kiri atas konten)
// color: indeks palette 0-15
static inline void win_draw_pixel(int id, int cx, int cy, unsigned char color) {
    syscall2(SYS_WIN_DRAW_PIXEL, id, (cx & 0xFFF) | ((cy & 0xFFF) << 12) | ((int)color << 24));
}

// Isi persegi panjang di area konten window dengan satu warna (jauh lebih cepat dari win_draw_pixel per-piksel)
typedef struct { short x, y, w, h; unsigned char color; } WinFillArgs;
static inline void win_fill_rect(int id, int x, int y, int w, int h, unsigned char color) {
    WinFillArgs a = { (short)x, (short)y, (short)w, (short)h, color };
    syscall2(SYS_WIN_FILL_RECT, id, (int)&a);
}

// Poll posisi mouse relatif area konten window + status tombol
// out_x, out_y: koordinat relatif (negatif jika di luar)
// return: button state (bit0=kiri, bit1=kanan)
static inline int win_mouse_rel(int id, int *out_x, int *out_y) {
    int buf[3];
    syscall2(SYS_WIN_MOUSE_REL, id, (int)buf);
    *out_x = buf[0];
    *out_y = buf[1];
    return buf[2];
}

// Minimize window sendiri (sembunyikan dari layar)
static inline void win_minimize(int id) {
    syscall1(SYS_WIN_MINIMIZE, id);
}

// Restore window dari minimized
static inline void win_restore(int id) {
    syscall1(SYS_WIN_RESTORE, id);
}

// List nama file ke buffer (dipisah '\n'), return jumlah file
static inline int fs_list(char *buf, int bufsz) {
    return syscall2(SYS_FS_LIST, (int)buf, bufsz);
}

// Hapus file berdasarkan nama, return 1 jika sukses
static inline int fs_delete(const char *name) {
    return syscall1(SYS_FS_DELETE, (int)name);
}

// Tampilkan kotak pesan modal dengan pesan dan tombol OK
// Blok hingga pengguna menutup atau klik OK
static inline void win_msgbox(const char *title, const char *msg) {
    int w = 300, h = 90;
    int x = (640 - w) / 2;
    int y = (480 - 16 - h) / 2;
    int id = win_create(x, y, w, h, title);
    if (id < 0) return;
    win_draw(id, 8, 8, msg, GFX_WHITE, GFX_BLACK);
    win_btn_add(id, (w - 2 - 60) / 2, 56, 60, 18, "OK");
    for (;;) {
        int ev = win_poll(id);
        if (ev == WIN_EVENT_NONE) { yield(); continue; }
        int t = WIN_EV_TYPE(ev);
        if (t == WIN_EVENT_CLOSE || t == WIN_EVENT_BTN) break;
    }
    win_destroy(id);
}

#endif
