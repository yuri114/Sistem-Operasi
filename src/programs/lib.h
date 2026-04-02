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
#define SYS_DRAW_CHAR  31  // gambar 1 karakter 8x8: ebx=ptr DrawCharArgs {int x,y; char c; char _pad[3]; uint32_t fg,bg;}
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
#define SYS_WIN_DRAW_PIXEL 41 // gambar piksel di konten window: ebx=id, edx=ptr WinPixelArgs
#define SYS_WIN_FILL_RECT  42 // isi persegi di konten window: ebx=id, edx=ptr WinFillArgs
#define SYS_WIN_MOUSE_REL  43 // posisi mouse relatif konten: ebx=id, edx=ptr int[3] → [rel_x, rel_y, btn]
#define SYS_WIN_MINIMIZE   44 // minimize window: ebx=id
#define SYS_WIN_RESTORE    45 // restore window dari minimized: ebx=id
#define SYS_FS_LIST        46 // list nama file ke buffer: ebx=ptr, edx=bufsz → return count
#define SYS_FS_DELETE      47 // hapus file: ebx=ptr nama → return 1/0
#define SYS_GET_TICKS      48 // timer tick sejak boot → return uint32_t
#define SYS_FS_SYNC        49 // flush dirty file ke disk → return count
#define SYS_FS_TMPWRITE    50 // tulis ke tmpfs: ebx=nama, edx=ptr FSWriteArgs
#define SYS_FS_MKDIR       51 // buat direktori: ebx=nama → return 1/0
#define SYS_PIPE_NAMED     52 // buka/buat named pipe: ebx=nama → return pipe_id
#define SYS_SHM_CREATE     53 // buat shared memory: ebx=key → return shm_id
#define SYS_SHM_ATTACH     54 // map shm ke proses: ebx=shm_id → return VA
#define SYS_SHM_DETACH     55 // lepas shm dari proses: ebx=shm_id

/* Tahap J — VFS */
#define SYS_OPEN      56  // buka/buat file: ebx=path_ptr, edx=flags
#define SYS_READ_FD   57  // baca fd: ebx=fd, edx=ptr{char*,int}
#define SYS_WRITE_FD  58  // tulis fd: ebx=fd, edx=ptr{const char*,int}
#define SYS_CLOSE_FD  59  // tutup fd: ebx=fd
/* Tahap J: flag open */
#define VFS_O_RDONLY  0x01
#define VFS_O_WRONLY  0x02
#define VFS_O_RDWR    0x03
#define VFS_O_CREATE  0x04

/* Fondasi: user heap + waitpid */
#define SYS_BRK       62  // perluas user heap: arg=new_end → return new_end aktual
#define SYS_WAITPID   63  // tunggu task selesai: arg=tid → return exit code

/* F-T — Signal & Process Control */
#define SYS_SIGACTION   87  // daftarkan handler sinyal: ebx=sig, edx=handler_va
#define SYS_SIGKILL_SIG 88  // kirim sinyal: ebx=tid, edx=sig → 0

/* Konstanta sinyal */
#define SIGINT   2
#define SIGKILL  9
#define SIGTERM  15

/* Tahap L — Message Queue */
#define SYS_MQ_SEND   60  // kirim MQ: ebx=dst_pid, edx=str_ptr
#define SYS_MQ_RECV   61  // terima MQ: ebx=ptr MqRecvResult

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

// Dimensi layar — cocok dengan graphics.h
#define SCREEN_W  1920
#define SCREEN_H  1080

// Warna 32bpp True Color (0x00RRGGBB) — cocok dengan VBE 32bpp linear frame buffer
#define GFX_BLACK    0x00000000u
#define GFX_BLUE     0x000000AAu
#define GFX_GREEN    0x0000AA00u
#define GFX_CYAN     0x0000AAAAu
#define GFX_RED      0x00AA0000u
#define GFX_MAGENTA  0x00AA00AAu
#define GFX_BROWN    0x00AA5500u
#define GFX_LGRAY    0x00AAAAAAu
#define GFX_DGRAY    0x00555555u
#define GFX_LBLUE    0x005555FFu
#define GFX_LGREEN   0x0055FF55u
#define GFX_LCYAN    0x0055FFFFu
#define GFX_LRED     0x00FF5555u
#define GFX_LMAGENTA 0x00FF55FFu
#define GFX_YELLOW   0x00FFFF55u
#define GFX_WHITE    0x00FFFFFFu

// Struct argumen syscall grafis — layout harus cocok dengan GfxRect/GfxLine di graphics.h
typedef struct { int x, y, w, h; unsigned int color; } GfxRect;
typedef struct { int x1, y1, x2, y2; unsigned int color; } GfxLine;

// ============================================================
// ============================================================
// syscall — memanggil kernel lewat SYSCALL instruction (D1)
//   rax = nomor syscall
//   rdi = arg1, rsi = arg2
//   return value ada di rax
//   rcx dan r11 di-clobber oleh SYSCALL
// ============================================================

// syscall1: rax=num, rdi=arg1
static inline long syscall1(long num, long arg) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// syscall2: rax=num, rdi=arg1, rsi=arg2
static inline long syscall2(long num, long arg1, long arg2) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// syscall0: rax=num
static inline long syscall0(long num) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// ============================================================
// Fungsi-fungsi untuk program user
// ============================================================

// Cetak string ke layar
static inline void print(const char *msg) {
    syscall1(SYS_PRINT, (long)msg);
}

// Tunggu input keyboard, kembalikan karakter yang ditekan
static inline char getkey() {
    return (char)syscall0(SYS_GETKEY);
}

// Keluar dari program dengan kode exit
static inline void exit_code(int code) {
    syscall1(SYS_EXIT, (long)code);
    while (1); // tidak pernah sampai sini
}

// Keluar dari program (kode 0)
static inline void exit() {
    exit_code(0);
}

// ============================================================
// User-space heap allocator (SYS_BRK based, block allocator)
// Heap region: 0x400000 – 0x5FE000 (~1.9MB)
// ============================================================
#define _USER_HEAP_START 0x400000UL
#define _USER_HEAP_LIMIT 0x5FE000UL  /* guard page batas atas */

typedef struct _UHdr {
    unsigned int   sz;    /* ukuran data (tidak termasuk header) */
    unsigned char  free;
    unsigned char  _p[3];
    struct _UHdr  *next;
} _UHdr;

#define _UHSZ ((unsigned int)sizeof(_UHdr))

static _UHdr         *_uheap = 0;
static unsigned long  _ubrk  = 0;
static int            _umtx  = -1;  /* semaphore mutex untuk thread-safe heap */

/* Forward declarations — definisi lengkap ada di bawah dekat syscall wrappers */
static int sem_alloc(int initial_value);
static int sem_wait(int id);
static int sem_post(int id);

/* Inisialisasi mutex pada panggilan malloc/free pertama.
 * Aman karena thread belum berjalan sebelum malloc pertama dipanggil induk. */
static inline void _umtx_init() {
    if (_umtx < 0) _umtx = sem_alloc(1);
}

static inline unsigned long sys_brk(unsigned long new_end) {
    return (unsigned long)syscall1(SYS_BRK, (long)new_end);
}

static inline void waitpid(int tid) {
    syscall1(SYS_WAITPID, (long)tid);
}

// F-T: tunggu task dan kembalikan exit code
static inline int waitpid_ex(int tid) {
    return (int)syscall1(SYS_WAITPID, (long)tid);
}

// F-T: kirim sinyal ke proses (seperti POSIX kill)
static inline int kill(int tid, int sig) {
    return (int)syscall2(SYS_SIGKILL_SIG, (long)tid, (long)sig);
}

static _UHdr *_uheap_grow(unsigned int need) {
    unsigned long want = _ubrk + (unsigned long)need + _UHSZ + 0xFFFUL;
    want &= ~0xFFFUL;
    if (want > _USER_HEAP_LIMIT) want = _USER_HEAP_LIMIT;
    if (want <= _ubrk) return 0;
    unsigned long got = sys_brk(want);
    if (got <= _ubrk) return 0;
    _UHdr *nb = (_UHdr*)_ubrk;
    nb->sz   = (unsigned int)(got - _ubrk - _UHSZ);
    nb->free = 1;
    nb->next = 0;
    /* Append to list */
    if (!_uheap) { _uheap = nb; }
    else {
        _UHdr *p = _uheap;
        while (p->next) p = p->next;
        p->next = nb;
    }
    _ubrk = got;
    return nb;
}

static inline void* malloc(int size) {
    if (size <= 0) return 0;
    _umtx_init();
    sem_wait(_umtx);
    unsigned int sz = (unsigned int)((size + 3) & ~3);
    /* Inisialisasi heap pada panggilan pertama */
    if (!_uheap) {
        _ubrk = _USER_HEAP_START;
        if (!_uheap_grow(sz)) { sem_post(_umtx); return 0; }
    }
    /* First-fit search */
    _UHdr *c = _uheap;
    while (c) {
        if (c->free && c->sz >= sz) {
            /* Split jika ada sisa cukup besar */
            if (c->sz >= sz + _UHSZ + 4u) {
                _UHdr *sp = (_UHdr*)((unsigned char*)(c + 1) + sz);
                sp->sz   = c->sz - sz - _UHSZ;
                sp->free = 1;
                sp->next = c->next;
                c->sz    = sz;
                c->next  = sp;
            }
            c->free = 0;
            sem_post(_umtx);
            return (void*)(c + 1);
        }
        if (!c->next) {
            /* Tidak ada slot bebas — perluas heap */
            _UHdr *nb = _uheap_grow(sz);
            if (!nb) { sem_post(_umtx); return 0; }
            /* Langsung pakai blok baru ini */
            continue;
        }
        c = c->next;
    }
    sem_post(_umtx);
    return 0;
}

static inline void free(void *ptr) {
    if (!ptr) return;
    _umtx_init();
    sem_wait(_umtx);
    _UHdr *blk = (_UHdr*)ptr - 1;
    blk->free = 1;
    /* Coalescing: gabung blok bebas bersebelahan */
    _UHdr *c = _uheap;
    while (c) {
        if (c->free && c->next && c->next->free) {
            c->sz  += _UHSZ + c->next->sz;
            c->next = c->next->next;
        } else {
            c = c->next;
        }
    }
    sem_post(_umtx);
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

// Baca file ke buffer user: salin isi file ke buf[0..bufsz-1], null-terminate.
// Kembalikan jumlah byte yang disalin, atau 0 jika file tidak ditemukan.
static inline int fs_read(const char *name, char *buf, int bufsz) {
    (void)bufsz; /* kernel memvalidasi pointer; UI layer bertanggung jawab ukuran buf */
    return (int)syscall2(SYS_FS_READ, (long)name, (long)buf);
}

// Tulis file: name = nama file, data = isi file (string)
// Kembalikan 1 jika sukses, 0 jika gagal (filesystem penuh)
static inline int fs_write(const char *name, const char *data) {
    // kirim dua argumen lewat satu pointer ke array of pointer
    const char *args[2];
    args[0] = name;
    args[1] = data;
    return syscall1(SYS_FS_WRITE, (long)args);
}

// ============================================================
// IPC — kirim dan terima pesan antar proses
// ============================================================

// Kirim pesan ke message queue kernel
// return 1 sukses, 0 jika queue penuh (maks 8 pesan)
static inline int msg_send(const char *msg) {
    return syscall1(SYS_MSG_SEND, (long)msg);
}

// Ambil satu pesan dari queue ke buffer buf (minimal 64 byte)
// return 1 jika ada pesan, 0 jika queue kosong
static inline int msg_recv(char *buf) {
    return syscall1(SYS_MSG_RECV, (long)buf);
}

// Terminasi paksa proses (tanpa sinyal, langsung)
// return 1 sukses, 0 gagal (id tidak valid / dilindungi)
static inline int task_kill(int id) {
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
    return syscall2(SYS_PIPE_WRITE, id, (long)msg);
}

// Baca satu pesan dari pipe ke buf — return bytes dibaca, 0 jika kosong
static inline int pipe_read(int id, char *buf) {
    return syscall2(SYS_PIPE_READ, id, (long)buf);
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
    return syscall2(SYS_DEV_WRITE, dev_id, (long)msg);
}

// Baca satu karakter dari device ke buf (misal: keyboard via DEV_KBD)
// Return 1 jika ada karakter, 0 jika kosong
static inline int dev_read(int dev_id, char *buf) {
    return syscall2(SYS_DEV_READ, dev_id, (long)buf);
}

// Kontrol khusus device (cmd dan arg dikemas dalam edx: cmd<<16 | arg)
static inline int dev_ioctl(int dev_id, int cmd, int arg) {
    return syscall2(SYS_DEV_IOCTL, dev_id, (cmd << 16) | (arg & 0xFFFF));
}

// ============================================================
// Grafis Mode 32bpp VBE — akses framebuffer via syscall
// ============================================================

// Gambar satu piksel di (x, y) dengan warna 32bpp
typedef struct { int x, y; unsigned int color; } DrawPixelArgs;
static inline void gfx_pixel(int x, int y, unsigned int color) {
    DrawPixelArgs a = {x, y, color};
    syscall1(SYS_DRAW_PIXEL, (long)&a);
}

// Isi seluruh layar dengan satu warna
static inline void gfx_fill(unsigned int color) {
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
    syscall1(SYS_SLEEP, (long)ms);
}

// Muat dan jalankan program dari filesystem (nama=string null-terminated)
// Return: task_id jika sukses, -1 jika gagal
static inline int exec(const char *name) {
    return syscall1(SYS_EXEC, (long)name);
}

// Gambar persegi panjang terisi menggunakan struct pointer
// (hindari variable length array di stack)
static inline void gfx_rect_s(GfxRect *r) {
    syscall1(SYS_FILL_RECT, (long)r);
}

// Gambar garis lurus menggunakan struct pointer
static inline void gfx_line_s(GfxLine *l) {
    syscall1(SYS_DRAW_LINE, (long)l);
}

// Shorthand: gambar persegi panjang tanpa deklarasi struct terpisah
static inline void gfx_rect(int x, int y, int w, int h, unsigned int color) {
    GfxRect r = {x, y, w, h, color};
    syscall1(SYS_FILL_RECT, (long)&r);
}

// Shorthand: gambar garis tanpa deklarasi struct terpisah
static inline void gfx_line(int x1, int y1, int x2, int y2, unsigned int color) {
    GfxLine l = {x1, y1, x2, y2, color};
    syscall1(SYS_DRAW_LINE, (long)&l);
}

// ============================================================
// Teks grafis (font 8x8) dan Mouse
// ============================================================

// Struct untuk menggambar string — layout harus cocok dengan GfxStr di graphics.h
typedef struct { int x, y; const char *s; unsigned int fg, bg; } GfxStr;

// State mouse — layout harus cocok dengan MouseState di mouse.h
typedef struct { int x, y; unsigned char buttons; } MouseState;

// Gambar satu karakter 8x8 di framebuffer pada (x,y)
static inline void gfx_char(int x, int y, char c, unsigned int fg, unsigned int bg) {
    typedef struct { int x, y; char c; char _pad[3]; unsigned int fg, bg; } DrawCharArgs;
    DrawCharArgs a = {x, y, c, {0,0,0}, fg, bg};
    syscall1(SYS_DRAW_CHAR, (long)&a);
}

// Gambar string null-terminated dengan font 8x8 di (x,y)
static inline void gfx_str(int x, int y, const char *s, unsigned int fg, unsigned int bg) {
    GfxStr g = {x, y, s, fg, bg};
    syscall1(SYS_DRAW_STR, (long)&g);
}

// Baca posisi dan status tombol mouse ke *ms
static inline void mouse_get(MouseState *ms) {
    syscall1(SYS_MOUSE_GET, (long)ms);
}

// ============================================================
// Window Manager
// ============================================================

// Struct argumen window — layout harus cocok dengan window.h di kernel
typedef struct { int x, y, w, h; const char *title; } WinCreateArgs;
typedef struct { int id, x, y; const char *s; unsigned int fg, bg; } WinDrawArgs;
typedef struct { int id, x, y, w, h; const char *label; } WinBtnArgs;

// Buat window baru, kembalikan id (0-15) atau -1 jika gagal
static inline int win_create(int x, int y, int w, int h, const char *title) {
    WinCreateArgs a = {x, y, w, h, title};
    return syscall1(SYS_WIN_CREATE, (long)&a);
}

// Tutup dan hapus window
static inline void win_destroy(int id) {
    syscall1(SYS_WIN_DESTROY, id);
}

// Gambar teks di area konten window pada offset piksel (px, py)
static inline void win_draw(int id, int px, int py, const char *s,
                             unsigned int fg, unsigned int bg) {
    WinDrawArgs d = {id, px, py, s, fg, bg};
    syscall1(SYS_WIN_DRAW, (long)&d);
}

// Bersihkan area konten window dengan warna bg
static inline void win_clear(int id, unsigned int bg) {
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
    return syscall1(SYS_WIN_BTN_ADD, (long)&a);
}

// Ambil koordinat klik konten terakhir (piksel relatif area konten window)
// Panggil setelah win_poll() return WIN_EVENT_CLICK
static inline void win_click_pos(int id, int *out_x, int *out_y) {
    int pos[2];
    syscall2(SYS_WIN_CLICK_POS, id, (long)pos);
    *out_x = pos[0];
    *out_y = pos[1];
}

// Gambar piksel warna di koordinat area konten window
// cx, cy: piksel relatif area konten (0,0 = pojok kiri atas konten)
// color: warna 32bpp (0x00RRGGBB)
typedef struct { int cx, cy; unsigned int color; } WinPixelArgs;
static inline void win_draw_pixel(int id, int cx, int cy, unsigned int color) {
    WinPixelArgs a = {cx, cy, color};
    syscall2(SYS_WIN_DRAW_PIXEL, id, (long)&a);
}

// Isi persegi panjang di area konten window dengan satu warna (jauh lebih cepat dari win_draw_pixel per-piksel)
typedef struct { short x, y, w, h; unsigned int color; } WinFillArgs;
static inline void win_fill_rect(int id, int x, int y, int w, int h, unsigned int color) {
    WinFillArgs a = { (short)x, (short)y, (short)w, (short)h, color };
    syscall2(SYS_WIN_FILL_RECT, id, (long)&a);
}

// Poll posisi mouse relatif area konten window + status tombol
// out_x, out_y: koordinat relatif (negatif jika di luar)
// return: button state (bit0=kiri, bit1=kanan)
static inline int win_mouse_rel(int id, int *out_x, int *out_y) {
    int buf[3];
    syscall2(SYS_WIN_MOUSE_REL, id, (long)buf);
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
    return syscall2(SYS_FS_LIST, (long)buf, bufsz);
}

// Hapus file berdasarkan nama, return 1 jika sukses
static inline int fs_delete(const char *name) {
    return syscall1(SYS_FS_DELETE, (long)name);
}

// Kembalikan jumlah timer tick sejak boot (18.2 tick/detik)
static inline unsigned int get_ticks(void) {
    return (unsigned int)syscall0(SYS_GET_TICKS);
}

// Flush semua dirty file ke disk, return jumlah file yang di-flush
static inline int fs_sync(void) {
    return (int)syscall0(SYS_FS_SYNC);
}

// Buat direktori baru, return 1 jika sukses
static inline int fs_mkdir(const char *name) {
    return (int)syscall1(SYS_FS_MKDIR, (long)name);
}

// Tulis data ke tmpfs (tidak persisten), return 1 jika sukses
static inline int fs_write_tmp(const char *name, const void *data, unsigned int size) {
    struct { const void *data; unsigned int size; } args;
    args.data = data;
    args.size = size;
    return (int)syscall2(SYS_FS_TMPWRITE, (long)name, (long)&args);
}

// Buka atau buat named pipe berdasarkan nama, return pipe_id atau -1
static inline int pipe_named_open(const char *name) {
    return (int)syscall1(SYS_PIPE_NAMED, (long)name);
}

// Buat shared memory dengan key, return shm_id atau -1
static inline int shm_create(const char *key) {
    return (int)syscall1(SYS_SHM_CREATE, (long)key);
}

// Map shared memory ke address space proses, return pointer atau NULL
static inline void *shm_attach(int shm_id) {
    return (void*)(long)syscall1(SYS_SHM_ATTACH, shm_id);
}

// Lepas shared memory dari address space proses
static inline void shm_detach(int shm_id) {
    syscall1(SYS_SHM_DETACH, shm_id);
}

/* ============================================================
 * Tahap J — VFS (file descriptor) wrappers
 * ============================================================ */
typedef struct { char *buf; int len; } VfsRWArgs;

static inline int sys_open(const char *path, int flags) {
    return (int)syscall2(SYS_OPEN, (long)path, flags);
}
static inline int sys_read_fd(int fd, char *buf, int len) {
    VfsRWArgs a = {buf, len};
    return (int)syscall2(SYS_READ_FD, fd, (long)&a);
}
static inline int sys_write_fd(int fd, const char *buf, int len) {
    VfsRWArgs a = {(char*)buf, len};
    return (int)syscall2(SYS_WRITE_FD, fd, (long)&a);
}
static inline int sys_close_fd(int fd) {
    return (int)syscall1(SYS_CLOSE_FD, fd);
}

/* ============================================================
 * Tahap L — Message Queue wrappers
 * ============================================================ */
typedef struct { int from; int len; char data[56]; } MqRecvResult;

static inline int mq_send(int dst_pid, const char *msg) {
    return (int)syscall2(SYS_MQ_SEND, dst_pid, (long)msg);
}
static inline int mq_recv(MqRecvResult *r) {
    return (int)syscall1(SYS_MQ_RECV, (long)r);
}

/* ============================================================
 * F2 — libc minimal
 *   va_list, sprintf/printf, string utils tambahan
 * ============================================================ */

/* Variadic arg support — GCC builtin, bekerja di freestanding */
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_arg(v,t)   __builtin_va_arg(v,t)
#define va_end(v)     __builtin_va_end(v)

/* Sambung src ke akhir dst (dst harus punya ruang cukup), return dst */
static inline char *strcat(char *dst, const char *src) {
    int i = 0;
    while (dst[i]) i++;
    int j = 0;
    while (src[j]) dst[i++] = src[j++];
    dst[i] = '\0';
    return dst;
}

/* Cari needle di hay, return pointer ke kemunculan pertama atau NULL */
static inline const char *strstr(const char *hay, const char *needle) {
    if (!needle[0]) return hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return hay;
    }
    return 0;
}

/* Konversi string ke long integer; base 0 = auto-detect (0x=hex, 0=octal, else decimal) */
static inline long strtol(const char *s, const char **endp, int base) {
    while (*s == ' ') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (base == 0 || base == 16) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (base == 0) base = (s[0] == '0') ? 8 : 10;
    }
    long r = 0;
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        r = r * base + d; s++;
    }
    if (endp) *endp = s;
    return neg ? -r : r;
}

/* String ke int (basis 10) */
static inline int atoi(const char *s) { return (int)strtol(s, 0, 10); }

/* Salin n byte dari src ke dst */
static inline void *memcpy(void *dst, const void *src, int n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *ss = (const unsigned char *)src;
    int i; for (i = 0; i < n; i++) d[i] = ss[i];
    return dst;
}

/* Isi n byte di dst dengan nilai c */
static inline void *memset(void *dst, int c, int n) {
    unsigned char *d = (unsigned char *)dst;
    int i; for (i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}

/* Salin n byte dengan aman saat src dan dst overlap */
static inline void *memmove(void *dst, const void *src, int n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *ss = (const unsigned char *)src;
    if (d < ss) { int i; for (i = 0;   i < n; i++) d[i] = ss[i]; }
    else         { int i; for (i = n-1; i >= 0; i--) d[i] = ss[i]; }
    return dst;
}

/* Bandingkan n byte: return 0 jika sama, non-0 jika berbeda */
static inline int memcmp(const void *a, const void *b, int n) {
    const unsigned char *ua = (const unsigned char *)a;
    const unsigned char *ub = (const unsigned char *)b;
    int i; for (i = 0; i < n; i++) if (ua[i] != ub[i]) return (int)ua[i] - (int)ub[i];
    return 0;
}

/* ---- sprintf / printf ---- */

/* Format string ke buf menggunakan va_list.
 * Specifier yang didukung: %d %i %u %x %X %s %c %p %% %ld %lu %lx
 * Flag: -, 0, lebar minimum (angka).
 * Return: panjang string yang dihasilkan (tidak termasuk null terminator). */
static inline int vsprintf(char *buf, const char *fmt, va_list ap) {
    int len = 0;
    while (*fmt) {
        if (*fmt != '%') { buf[len++] = *fmt++; continue; }
        fmt++; /* lewati '%' */
        /* parse flags */
        int left = 0, zpad = 0, width = 0, lng = 0;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zpad = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt++ - '0'); }
        if (*fmt == 'l') { lng = 1; fmt++; }
        char sp = *fmt++;
        /* simple specifiers */
        if (sp == '%') { buf[len++] = '%'; continue; }
        if (sp == 'c') { buf[len++] = (char)va_arg(ap, int); continue; }
        if (sp == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int sl = 0; while (s[sl]) sl++;
            int pd = (width > sl) ? width - sl : 0;
            if (!left) { int i; for (i = 0; i < pd; i++) buf[len++] = ' '; }
            while (*s) buf[len++] = *s++;
            if (left)  { int i; for (i = 0; i < pd; i++) buf[len++] = ' '; }
            continue;
        }
        /* numeric specifiers */
        unsigned long uv = 0; int neg = 0;
        if (sp == 'd' || sp == 'i') {
            long v = lng ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (v < 0) { neg = 1; uv = (unsigned long)(-v); } else uv = (unsigned long)v;
        } else if (sp == 'u') {
            uv = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
        } else if (sp == 'x' || sp == 'X') {
            uv = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
        } else if (sp == 'p') {
            uv = (unsigned long)va_arg(ap, void *); sp = 'x';
        } else {
            buf[len++] = sp; continue;
        }
        int base = (sp == 'x' || sp == 'X') ? 16 : 10;
        const char *dl = (sp == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
        char tmp[24]; int ti = 0;
        if (uv == 0) { tmp[ti++] = '0'; }
        else { while (uv) { tmp[ti++] = dl[uv % base]; uv /= base; } }
        if (neg) tmp[ti++] = '-';
        int pd = (width > ti) ? width - ti : 0;
        char pch = (zpad && !left) ? '0' : ' ';
        if (!left) { int i; for (i = 0; i < pd; i++) buf[len++] = pch; }
        { int k; for (k = ti - 1; k >= 0; k--) buf[len++] = tmp[k]; }
        if (left)  { int i; for (i = 0; i < pd; i++) buf[len++] = ' '; }
    }
    buf[len] = '\0';
    return len;
}

/* sprintf: format string ke buffer, return panjang */
static inline int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

/* printf: format ke buffer 512 byte lalu cetak via SYS_PRINT */
static inline void printf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    print(buf);
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

/* ============================================================
 * Tahap N — User-space Threading
 * ============================================================ */
#define SYS_THREAD_CREATE 64  /* buat thread: arg1=entry_va, arg2=arg → return tid */
#define SYS_THREAD_EXIT   65  /* keluar dari thread saat ini                       */
#define SYS_THREAD_JOIN   66  /* tunggu thread: arg1=tid → return 0                */

/* Fondasi P — Thread naming + Condition Variable */
#define SYS_THREAD_SET_NAME 67  /* set nama thread: arg1=tid, arg2=name_ptr */
#define SYS_COND_ALLOC      68  /* alokasi condvar → return id              */
#define SYS_COND_FREE       69  /* bebaskan condvar: arg1=id                */
#define SYS_COND_WAIT       70  /* cv_wait: arg1=cond_id, arg2=sem_id       */
#define SYS_COND_SIGNAL     71  /* bangunkan 1 waiter: arg1=cond_id         */
#define SYS_COND_BROADCAST  72  /* bangunkan semua waiter: arg1=cond_id     */

/* typedef untuk fungsi thread: void fn(void *arg) */
typedef void (*thread_fn_t)(void *);

/* Buat thread baru yang menjalankan fn(arg) dalam address space yang sama.
 * Return: tid thread, atau -1 jika gagal (slot habis).
 * PENTING: fn() HARUS memanggil thread_exit() sebelum return. */
static inline int thread_create(thread_fn_t fn, void *arg) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"((long)SYS_THREAD_CREATE), "D"((long)fn), "S"((long)arg)
        : "rcx", "r11", "memory"
    );
    return (int)ret;
}

/* Keluar dari thread saat ini. Wajib dipanggil di akhir fungsi thread. */
static inline void thread_exit(void) {
    __asm__ volatile (
        "syscall"
        :: "a"((long)SYS_THREAD_EXIT)
        : "rcx", "r11", "memory"
    );
    while (1); /* tidak pernah kembali */
}

/* Tunggu thread tid selesai (blocking join). */
static inline void thread_join(int tid) {
    __asm__ volatile (
        "syscall"
        :: "a"((long)SYS_THREAD_JOIN), "D"((long)tid)
        : "rcx", "r11", "memory"
    );
}

/* Set nama thread (ditampilkan di debug/taskbar). */
static inline void thread_set_name(int tid, const char *name) {
    syscall2(SYS_THREAD_SET_NAME, (long)tid, (long)name);
}

/* Alokasi condition variable baru, return id atau -1 jika penuh. */
static inline int cond_alloc(void) {
    return (int)syscall0(SYS_COND_ALLOC);
}

/* Bebaskan condition variable. */
static inline void cond_free(int id) {
    syscall1(SYS_COND_FREE, (long)id);
}

/* Atomik: lepas mutex sem_id, block, lalu reacquire setelah dibangunkan.
 * HARUS dipanggil saat memiliki mutex (sem_id sudah di-wait). */
static inline int cond_wait(int id, int sem_id) {
    return (int)syscall2(SYS_COND_WAIT, (long)id, (long)sem_id);
}

/* Bangunkan satu waiter pada condvar. */
static inline int cond_signal(int id) {
    return (int)syscall1(SYS_COND_SIGNAL, (long)id);
}

/* Bangunkan semua waiter pada condvar. */
static inline int cond_broadcast(int id) {
    return (int)syscall1(SYS_COND_BROADCAST, (long)id);
}

// ============================================================
// Fondasi Q — Proses & Memori Lanjutan
// ============================================================

#define SYS_FORK         73
#define SYS_EXEC_REPLACE 74
#define SYS_MMAP         75
#define SYS_MUNMAP       76

/*
 * fork() — duplikasi proses.
 * Return: 0 di anak, tid anak di induk, -1 jika gagal.
 *
 * Menggunakan int 0x80 (bukan syscall) agar kernel bisa membaca RSP user
 * langsung dari frame iretq yang dibuat CPU (reliable, tidak bergantung
 * pada userspace argument passing).
 *
 * Kernel mengambil: child_rip dari [rsp+15*8+0] (RIP di iretq frame),
 *                   child_rsp dari [rsp+15*8+24] (RSP di iretq frame).
 * User hanya mengirimkan SYS_FORK (rax=73), kernel compute sisanya sendiri.
 *
 * Induk mendapat rax = tid anak; anak mendapat rax = 0 (dari task_create_fork).
 */
static inline int fork(void) {
    int ret;
    __asm__ volatile (
        "int $0x80\n\t"
        : "=a"(ret)
        : "a"((long)SYS_FORK)
        : "rbx", "rcx", "rdx", "rsi", "rdi",
          "r8",  "r9",  "r10", "r11", "r12",
          "r13", "r14", "r15", "memory"
    );
    return ret;
}

/*
 * exec_replace(name) — ganti image proses dengan program lain.
 * Jika berhasil, tidak pernah kembali ke pemanggil.
 * Return -1 jika gagal (program tidak ditemukan).
 */
static inline int exec_replace(const char *name) {
    return (int)syscall1(SYS_EXEC_REPLACE, (long)name);
}

/*
 * mmap(n_pages) — alokasi n_pages halaman anonim.
 * Return: pointer ke area yang dipetakan, atau NULL jika gagal.
 */
static inline void *mmap(int n_pages) {
    return (void *)syscall1(SYS_MMAP, (long)(unsigned)n_pages);
}

/*
 * munmap(addr, n_pages) — bebaskan area mmap.
 */
static inline void munmap(void *addr, int n_pages) {
    syscall2(SYS_MUNMAP, (long)addr, (long)(unsigned)n_pages);
}

/* ------------------------------------------------------------------ */
/* F-R2 — VFS backend: pipe / net / tty                               */
/* ------------------------------------------------------------------ */

#define SYS_PIPE2          77
#define SYS_NET_OPEN       78
#define SYS_TTY_OPEN       79
#define SYS_PIPE_REDIRECT  80

/* F-R3 — MFS4 inode layer                                            */
#define SYS_MFS4_SYMLINK   81
#define SYS_MFS4_HARDLINK  82
#define SYS_MFS4_STAT      83
#define SYS_MFS4_LISTDIR   84
#define SYS_MFS4_UNLINK    85
#define SYS_MFS4_MKDIR     86

/*
 * pipe2(fds) — buat anonymous pipe.
 * fds[0] = read-end fd, fds[1] = write-end fd.
 * Return 0 sukses, -1 gagal.
 */
static inline int pipe2(int fds[2]) {
    return (int)syscall1(SYS_PIPE2, (long)fds);
}

/*
 * net_open_fd(ip, port) — buka TCP connection, return fd.
 */
typedef struct { unsigned char ip[4]; unsigned short port; } _NetOpenArgs;
static inline int net_open_fd(unsigned char ip[4], unsigned short port) {
    _NetOpenArgs a;
    int i; for (i = 0; i < 4; i++) a.ip[i] = ip[i];
    a.port = port;
    return (int)syscall1(SYS_NET_OPEN, (long)&a);
}

/*
 * tty_open() — buka TTY baru, return fd.
 */
static inline int tty_open(void) {
    return (int)syscall0(SYS_TTY_OPEN);
}

/*
 * pipe_redirect_out(tid, pipe_id) — redirect stdout task tid ke write-end pipe_id.
 * pipe_redirect_in(tid, pipe_id)  — redirect stdin  task tid ke read-end  pipe_id.
 */
static inline int pipe_redirect_out(int tid, int pipe_id) {
    return (int)syscall2(SYS_PIPE_REDIRECT, (long)tid,
                         (long)((1 << 24) | (pipe_id & 0xFFFFFF)));
}
static inline int pipe_redirect_in(int tid, int pipe_id) {
    return (int)syscall2(SYS_PIPE_REDIRECT, (long)tid,
                         (long)((0 << 24) | (pipe_id & 0xFFFFFF)));
}

/* ------------------------------------------------------------------ */
/* F-R3 — MFS4 inode layer wrappers                                   */
/* ------------------------------------------------------------------ */

/* struct stat dari MFS4 (harus cocok dengan MFS4Stat di kernel) */
typedef struct {
    unsigned char  type;   /* 1=file, 2=dir, 3=symlink */
    unsigned int   size;
    unsigned int   ctime;
    unsigned int   mtime;
    unsigned short perms;
} UMfs4Stat;

static inline int mfs4_symlink(const char *link, const char *target) {
    return (int)syscall2(SYS_MFS4_SYMLINK, (long)link, (long)target);
}
static inline int mfs4_hardlink(const char *link, const char *original) {
    return (int)syscall2(SYS_MFS4_HARDLINK, (long)link, (long)original);
}
static inline int mfs4_stat(const char *path, UMfs4Stat *out) {
    return (int)syscall2(SYS_MFS4_STAT, (long)path, (long)out);
}

typedef struct { char *buf; int bufsz; } _MListArgs;
static inline int mfs4_listdir(const char *dir, char *buf, int bufsz) {
    _MListArgs a = { buf, bufsz };
    return (int)syscall2(SYS_MFS4_LISTDIR, (long)dir, (long)&a);
}
static inline int mfs4_unlink(const char *path) {
    return (int)syscall1(SYS_MFS4_UNLINK, (long)path);
}
static inline int mfs4_mkdir(const char *path) {
    return (int)syscall1(SYS_MFS4_MKDIR, (long)path);
}

#endif
