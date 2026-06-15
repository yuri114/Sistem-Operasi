#include "condvar.h"
#include "syscall.h"
#include "keyboard.h"
#include "task.h"
#include "memory.h"
#include "fs.h"
#include "mfs4.h"
#include "ipc.h"
#include "semaphore.h"
#include "pipe.h"
#include "device.h"
#include "graphics.h"
#include "vmm.h"
#include "elf_loader.h"
#include "mouse.h"
#include "window.h"
#include "timer.h"
#include "shm.h"
#include "vfs.h"
#include "mq.h"
#include "net.h"

extern void print(const char *str); // dari kernel.c
extern void clear_screen();         // dari kernel.c

/* -----------------------------------------------------------------------
 * AT4: Clipboard — buffer kernel 512 byte, dapat diakses via syscall
 * ----------------------------------------------------------------------- */
static char g_clipboard[512];
static int  g_clip_len = 0;

/* Salin string ke clipboard (dipanggil dari shell atau via syscall) */
void clip_copy(const char *s) {
    int i = 0;
    while (s[i] && i < 511) { g_clipboard[i] = s[i]; i++; }
    g_clipboard[i] = '\0';
    g_clip_len = i;
}

/* Tempel clipboard ke buffer dst (max maxlen-1 karakter + null). Return panjang. */
int clip_paste(char *dst, int maxlen) {
    int i = 0;
    while (i < g_clip_len && i < maxlen - 1) { dst[i] = g_clipboard[i]; i++; }
    dst[i] = '\0';
    return i;
}

/* -----------------------------------------------------------------------
 * F-U1: Futex table — maks 32 waiter sekaligus
 * ----------------------------------------------------------------------- */
typedef struct {
    uint64_t addr;  /* alamat virtual user yang ditunggu */
    int      tid;   /* task yang menunggu */
    int      used;  /* 1 = slot terpakai */
} FutexEntry;

#define FUTEX_TABLE_SIZE 32
static FutexEntry futex_table[FUTEX_TABLE_SIZE];

/* Validasi pointer dari user space: tolak NULL dan pointer ke kernel space (<0x300000).
 * User programs dimuat di 0x300000+. Stack user di 0x400000+. */
static int is_user_ptr(uint64_t ptr) {
    return (ptr != 0 && ptr >= 0x300000ULL);
}

uint64_t syscall_handler(uint64_t eax, uint64_t ebx, uint64_t edx) {
    /* F-T: Cek sinyal pending sebelum memproses syscall apapun.
     * Jika ada SIGKILL/SIGTERM/SIGINT, terminasi task ini sekarang. */
    task_check_signals();

    if (eax == SYS_PRINT){
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        const char *s = (const char*)ebx;
        int tid = task_get_current();
        /* Jika fd 1 task ini diredirect ke file, tulis via VFS bukan layar */
        int n = 0; while (s[n]) n++;
        if (vfs_stdout_is_file(tid))
            vfs_write(tid, 1, s, n);
        else
            print(s);
        return 0;
    }
    if (eax == SYS_GETKEY){
        char c = 0;
        __asm__ volatile ("sti");
        while (c == 0) {
            c = keyboard_getchar(); //ambil karakter dari buffer keyboard
        }
        return (uint32_t) (unsigned char)c; //kembalikan karakter sebagai uint32_t
    }
    if (eax == SYS_EXIT){
        task_exit_code((int)(int64_t)ebx); /* F-T: simpan kode exit */
        return 0; /* tidak pernah dicapai */
    }
    if (eax == SYS_ALLOC) {
        void* ptr = malloc(ebx); //ebx berisi ukuran memori yang akan dialokasikan
        return (uint32_t)ptr; //kembalikan pointer ke memori yang dialokasikan
    }
    if (eax == SYS_FREE) {
        free((void*)ebx); //ebx berisi pointer ke memori yang akan dibebaskan
        return 0; //kembalikan 0 untuk menandakan sukses
    }

    // SYS_FS_READ(5): ebx = pointer nama file (userspace), edx = pointer buffer tujuan (userspace)
    // Kernel menyalin isi file ke buffer user. Return: jumlah byte yang disalin, atau 0 jika gagal.
    if (eax == SYS_FS_READ) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return 0;
        const char *data = fs_read((const char*)ebx);
        if (!data) return 0;
        char *ubuf = (char*)edx;
        uint32_t n = 0;
        while (data[n] && n < FS_MAX_DATA - 1u) { ubuf[n] = data[n]; n++; }
        ubuf[n] = '\0';
        return n;
    }
    // SYS_FS_WRITE(6): ebx = pointer ke struct { const char *name; const char *data; }
    // return: 1 sukses, 0 gagal
    if (eax == SYS_FS_WRITE) {
        if (!is_user_ptr(ebx)) return 0;
        const char **args = (const char**)ebx;
        if (!is_user_ptr((uint64_t)args[0]) || !is_user_ptr((uint64_t)args[1])) return 0;
        const char *name = args[0];
        const char *data = args[1];
        return (uint32_t)fs_write(name, data);
    }

    // SYS_MSG_SEND(7): ebx = pointer string pesan
    if (eax == SYS_MSG_SEND) {
        if (!is_user_ptr(ebx)) return 0;
        return (uint32_t)ipc_send((const char*)ebx);
    }
    // SYS_MSG_RECV(8): ebx = pointer buffer tujuan (minimal 64 byte)
    if (eax == SYS_MSG_RECV) {
        if (!is_user_ptr(ebx)) return 0;
        return (uint32_t)ipc_recv((char*)ebx);
    }

    // SYS_KILL(9): ebx = id proses yang akan dimatikan
    if (eax == SYS_KILL) {
        return (uint32_t)task_kill((int)ebx);
    }

    // SYS_SEM_ALLOC(10): ebx = nilai awal (biasanya 1)
    if (eax == SYS_SEM_ALLOC) {
        return (uint32_t)sem_alloc((int)ebx);
    }
    // SYS_SEM_FREE(11): ebx = id semaphore
    if (eax == SYS_SEM_FREE) {
        sem_free((int)ebx);
        return 0;
    }
    // SYS_SEM_WAIT(12): ebx = id semaphore — block sampai bebas
    if (eax == SYS_SEM_WAIT) {
        return (uint32_t)sem_wait((int)ebx);
    }
    // SYS_SEM_POST(13): ebx = id semaphore — release
    if (eax == SYS_SEM_POST) {
        return (uint32_t)sem_post((int)ebx);
    }

    // SYS_PIPE_OPEN(14): alokasi pipe baru — return id (0-7) atau -1
    if (eax == SYS_PIPE_OPEN) {
        return (uint32_t)pipe_alloc();
    }
    // SYS_PIPE_WRITE(15): ebx=id, edx=pointer string — tulis ke pipe
    if (eax == SYS_PIPE_WRITE) {
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        return (uint32_t)pipe_write((int)ebx, (const char*)edx);
    }
    // SYS_PIPE_READ(16): ebx=id, edx=pointer buffer — baca satu pesan dari pipe
    if (eax == SYS_PIPE_READ) {
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        return (uint32_t)pipe_read((int)ebx, (char*)edx);
    }
    // SYS_PIPE_CLOSE(17): ebx=id — bebaskan slot pipe
    if (eax == SYS_PIPE_CLOSE) {
        pipe_free((int)ebx);
        return 0;
    }
    // SYS_PIPE_GETID(18): kembalikan pipe_id yang diwarisi task saat ini dari shell
    if (eax == SYS_PIPE_GETID) {
        return (uint32_t)task_get_current_pipe();
    }

    // SYS_DEV_WRITE(19): ebx=dev_id, edx=pointer string
    if (eax == SYS_DEV_WRITE) {
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        return (uint32_t)dev_write((int)ebx, (const char*)edx);
    }
    // SYS_DEV_READ(20): ebx=dev_id, edx=pointer buffer
    if (eax == SYS_DEV_READ) {
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        return (uint32_t)dev_read((int)ebx, (char*)edx);
    }
    // SYS_DEV_IOCTL(21): ebx=dev_id, edx=cmd<<16|arg
    if (eax == SYS_DEV_IOCTL) {
        int cmd = (int)((edx >> 16) & 0xFFFF);
        int arg = (int)(edx & 0xFFFF);
        return (uint32_t)dev_ioctl((int)ebx, cmd, arg);
    }

    // SYS_DRAW_PIXEL(22): gambar satu piksel di framebuffer
    // ebx = ptr ke struct { int x, y; uint32_t color; }
    if (eax == SYS_DRAW_PIXEL) {
        typedef struct { int x, y; uint32_t color; } DrawPixelArgs;
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        DrawPixelArgs *a = (DrawPixelArgs*)ebx;
        draw_pixel(a->x, a->y, a->color);
        return 0;
    }
    // SYS_FILL_SCREEN(23): ebx=color 32bpp — isi seluruh layar
    if (eax == SYS_FILL_SCREEN) {
        fill_screen((uint32_t)ebx);
        return 0;
    }
    // SYS_FILL_RECT(24): ebx=pointer ke GfxRect
    if (eax == SYS_FILL_RECT) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        GfxRect *r = (GfxRect*)ebx;
        fill_rect(r->x, r->y, r->w, r->h, r->color);
        return 0;
    }
    // SYS_DRAW_LINE(25): ebx=pointer ke GfxLine — gambar garis Bresenham
    if (eax == SYS_DRAW_LINE) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        GfxLine *l = (GfxLine*)ebx;
        draw_line(l->x1, l->y1, l->x2, l->y2, l->color);
        return 0;
    }
    // SYS_CLR_SCREEN(26): bersihkan layar + reset kursor ke (0,0)
    if (eax == SYS_CLR_SCREEN) {
        clear_screen();
        return 0;
    }

    // SYS_GETPID(27): kembalikan id task yang sedang berjalan
    if (eax == SYS_GETPID) {
        return (uint32_t)task_get_current();
    }

    // SYS_YIELD(28): lepas sisa slot CPU ke task lain pada tick berikutnya
    if (eax == SYS_YIELD) {
        task_yield();
        __asm__ volatile ("sti");
        __asm__ volatile ("hlt"); // tunggu satu timer tick
        return 0;
    }

    // SYS_SLEEP(29): tidur ebx milidetik
    if (eax == SYS_SLEEP) {
        task_sleep(ebx);
        return 0;
    }

    // SYS_DRAW_CHAR(31): gambar satu karakter 8x8 di framebuffer
    // ebx = ptr ke struct { int x, y; char c; char _pad[3]; uint32_t fg, bg; }
    if (eax == SYS_DRAW_CHAR) {
        typedef struct { int x, y; char c; char _pad[3]; uint32_t fg, bg; } DrawCharArgs;
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        DrawCharArgs *a = (DrawCharArgs*)ebx;
        draw_char_gfx(a->x, a->y, a->c, a->fg, a->bg);
        return 0;
    }

    // SYS_DRAW_STR(32): gambar string di framebuffer
    // ebx = ptr ke GfxStr { int x, y; const char *s; uint32_t fg, bg; }
    if (eax == SYS_DRAW_STR) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        GfxStr *gs = (GfxStr*)ebx;
        if (!is_user_ptr((uint64_t)gs->s)) return (uint64_t)-1;
        draw_string_gfx(gs->x, gs->y, gs->s, gs->fg, gs->bg);
        return 0;
    }

    // SYS_MOUSE_GET(33): baca state mouse ke struct MouseState
    // ebx = ptr ke MouseState { int x, y; uint8_t buttons; }
    if (eax == SYS_MOUSE_GET) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        mouse_get_state((MouseState*)ebx);
        return 0;
    }

    // SYS_WIN_CREATE(34): buat window baru
    // ebx = ptr ke WinCreateArgs { int x, y, w, h; const char *title; }
    // return: window id (0..MAX_WINDOWS-1) atau -1 jika gagal
    if (eax == SYS_WIN_CREATE) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        WinCreateArgs *a = (WinCreateArgs*)ebx;
        if (a->title && !is_user_ptr((uint64_t)a->title)) return (uint64_t)-1;
        return (uint64_t)wm_create(a->x, a->y, a->w, a->h, a->title);
    }
    // SYS_WIN_DESTROY(35): tutup window, bebaskan slot
    // ebx = window id
    if (eax == SYS_WIN_DESTROY) {
        wm_destroy((int)ebx);
        return 0;
    }
    // SYS_WIN_DRAW(36): gambar teks di area konten window
    // ebx = ptr ke WinDrawArgs { int id, x, y; const char *s; uint32_t fg, bg; }
    if (eax == SYS_WIN_DRAW) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        WinDrawArgs *d = (WinDrawArgs*)ebx;
        if (!is_user_ptr((uint64_t)d->s)) return (uint64_t)-1;
        wm_draw_text(d->id, d->x, d->y, d->s, d->fg, d->bg);
        return 0;
    }
    // SYS_WIN_CLEAR(37): bersihkan area konten window dengan warna bg 32bpp
    // ebx = window id, edx = warna background (uint32_t)
    if (eax == SYS_WIN_CLEAR) {
        wm_clear_content((int)ebx, (uint32_t)edx);
        return 0;
    }
    // SYS_WIN_EVENT(38): ambil event dari antrian window
    // ebx = window id; return WIN_EVENT_* (encode char/btn di byte atas)
    if (eax == SYS_WIN_EVENT) {
        return (uint32_t)wm_poll_event((int)ebx);
    }

    // SYS_WIN_BTN_ADD(39): tambah tombol ke window
    // ebx = ptr WinBtnArgs; return btn_idx atau -1
    if (eax == SYS_WIN_BTN_ADD) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        WinBtnArgs *a = (WinBtnArgs*)ebx;
        if (a->label && !is_user_ptr((uint64_t)a->label)) return (uint64_t)-1;
        return (uint64_t)wm_btn_add(a->id, a->x, a->y, a->w, a->h, a->label);
    }

    // SYS_WIN_CLICK_POS(40): ambil koordinat klik konten terakhir
    // ebx = window id; edx = ptr int[2] (output x, y relatif konten)
    if (eax == SYS_WIN_CLICK_POS) {
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        int *pos = (int*)edx;
        wm_get_click_pos((int)ebx, &pos[0], &pos[1]);
        return 0;
    }

    // SYS_WIN_DRAW_PIXEL(41): gambar piksel di koordinat area konten window
    // ebx = window id; edx = ptr ke struct { int cx, cy; uint32_t color; }
    if (eax == SYS_WIN_DRAW_PIXEL) {
        typedef struct { int cx, cy; uint32_t color; } WinPixelArgs;
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        WinPixelArgs *a = (WinPixelArgs*)edx;
        wm_draw_pixel((int)ebx, a->cx, a->cy, a->color);
        return 0;
    }

    // SYS_WIN_FILL_RECT(42): isi persegi di area konten window
    // ebx = window id; edx = ptr ke struct { short x,y,w,h; uint32_t color; }
    if (eax == SYS_WIN_FILL_RECT) {
        typedef struct { short x, y, w, h; uint32_t color; } WinFillArgs;
        if (!is_user_ptr(edx)) return 0;
        WinFillArgs *a = (WinFillArgs*)edx;
        wm_fill_rect((int)ebx, a->x, a->y, a->w, a->h, a->color);
        return 0;
    }

    // SYS_WIN_MOUSE_REL(43): posisi mouse relatif area konten window
    // ebx = window id; edx = ptr int[3] → [rel_x, rel_y, btn_state]
    if (eax == SYS_WIN_MOUSE_REL) {
        if (!is_user_ptr(edx)) return 0;
        int *out = (int*)edx;
        out[2] = wm_mouse_rel((int)ebx, &out[0], &out[1]);
        return 0;
    }

    // SYS_WIN_MINIMIZE(44): minimize window
    if (eax == SYS_WIN_MINIMIZE) {
        wm_minimize_by_id((int)ebx);
        return 0;
    }

    // SYS_WIN_RESTORE(45): restore window dari minimized
    if (eax == SYS_WIN_RESTORE) {
        wm_restore_by_id((int)ebx);
        return 0;
    }

    // SYS_FS_LIST(46): list nama file ke buffer
    if (eax == SYS_FS_LIST) {
        if (!is_user_ptr(ebx)) return 0;
        if ((int)edx <= 0) return 0;
        return (uint32_t)fs_list_buf((char*)ebx, (int)edx);
    }

    // SYS_FS_DELETE(47): hapus file berdasarkan nama
    if (eax == SYS_FS_DELETE) {
        if (!is_user_ptr(ebx)) return 0;
        return (uint32_t)fs_delete((const char*)ebx);
    }

    // SYS_GET_TICKS(48): kembalikan jumlah timer tick sejak boot
    if (eax == SYS_GET_TICKS) {
        return get_ticks();
    }

    // SYS_FS_SYNC(49): flush semua dirty file ke disk
    if (eax == SYS_FS_SYNC) {
        return (uint64_t)fs_flush();
    }

    // SYS_FS_TMPWRITE(50): tulis data ke tmpfs: ebx=nama, edx=ptr FSWriteArgs
    if (eax == SYS_FS_TMPWRITE) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return 0;
        typedef struct { const uint8_t *data; uint32_t size; } WArgs;
        WArgs *a = (WArgs*)edx;
        return (uint64_t)fs_write_tmp((const char*)ebx, a->data, a->size);
    }

    // SYS_FS_MKDIR(51): buat direktori: ebx=nama
    if (eax == SYS_FS_MKDIR) {
        if (!is_user_ptr(ebx)) return 0;
        return (uint64_t)fs_mkdir((const char*)ebx);
    }

    // SYS_PIPE_NAMED(52): buka/buat named pipe: ebx=nama
    if (eax == SYS_PIPE_NAMED) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        return (uint64_t)named_pipe_open((const char*)ebx);
    }

    // SYS_SHM_CREATE(53): buat shared memory segment: ebx=key
    if (eax == SYS_SHM_CREATE) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        return (uint64_t)shm_create((const char*)ebx);
    }

    // SYS_SHM_ATTACH(54): map shm segment ke proses pemanggil: ebx=shm_id
    if (eax == SYS_SHM_ATTACH) {
        int tid = task_get_current();
        uint64_t *pml4 = task_get_page_dir(tid);
        return (uint64_t)shm_attach((int)ebx, pml4);
    }

    // SYS_SHM_DETACH(55): lepas shm dari proses: ebx=shm_id
    if (eax == SYS_SHM_DETACH) {
        int tid = task_get_current();
        uint64_t *pml4 = task_get_page_dir(tid);
        shm_detach((int)ebx, pml4);
        return 0;
    }

    // SYS_EXEC(30)
    if (eax == SYS_EXEC) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        const char *name = (const char*)ebx;
        uint32_t sz = 0;
        const uint8_t *data = fs_read_bin(name, &sz);
        if (!data || sz == 0) return (uint64_t)-1;
        uint64_t *proc_dir = vmm_create_page_dir();
        uint64_t entry = elf_load(data, sz, proc_dir);
        if (!entry) return (uint64_t)-1;
        uint64_t stack_phys = pmm_alloc_frame();
        vmm_map_page(proc_dir, 0x600000, stack_phys, 7);
        int tid = task_create_user(entry, proc_dir, 0x600000 + 0x1000, name, 0, 0);
        return (uint64_t)tid;
    }

    /* ---- Tahap J: VFS syscalls ---- */
    // SYS_OPEN(56): buka file; ebx=path_ptr, edx=flags
    if (eax == SYS_OPEN) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        int tid = task_get_current();
        return (uint64_t)vfs_open(tid, (const char*)ebx, (int)edx);
    }
    // SYS_READ_FD(57): baca fd; ebx=fd, edx=ptr{char*buf, int len}
    if (eax == SYS_READ_FD) {
        if (!is_user_ptr(edx)) return (uint64_t)-1;
        typedef struct { char *buf; int len; } RArgs;
        RArgs *a = (RArgs*)edx;
        if (!is_user_ptr((uint64_t)a->buf)) return (uint64_t)-1;
        if (a->len < 0 || a->len > 65536) return (uint64_t)-1;
        int tid = task_get_current();
        return (uint64_t)vfs_read(tid, (int)ebx, a->buf, a->len);
    }
    // SYS_WRITE_FD(58): tulis fd; ebx=fd, edx=ptr{const char*buf, int len}
    if (eax == SYS_WRITE_FD) {
        if (!is_user_ptr(edx)) return (uint64_t)-1;
        typedef struct { const char *buf; int len; } WArgs;
        WArgs *a = (WArgs*)edx;
        if (!is_user_ptr((uint64_t)a->buf)) return (uint64_t)-1;
        if (a->len < 0 || a->len > 65536) return (uint64_t)-1;
        int tid = task_get_current();
        return (uint64_t)vfs_write(tid, (int)ebx, a->buf, a->len);
    }
    // SYS_CLOSE_FD(59): tutup fd; ebx=fd
    if (eax == SYS_CLOSE_FD) {
        int tid = task_get_current();
        return (uint64_t)vfs_close(tid, (int)ebx);
    }

    /* ---- Tahap L: Message Queue syscalls ---- */
    // SYS_MQ_SEND(60): kirim pesan; ebx=dst_pid, edx=str_ptr (max 56 char)
    if (eax == SYS_MQ_SEND) {
        if (!is_user_ptr(edx)) return (uint64_t)-1;
        const char *msg = (const char*)edx;
        int from = task_get_current();
        /* hitung panjang (bounded) */
        int len = 0;
        while (len < MQ_MSG_SIZE && msg[len]) len++;
        return (uint64_t)(mq_send((int)ebx, (const uint8_t*)msg, len, from) == 0 ? 1 : 0);
    }
    // SYS_MQ_RECV(61): terima pesan; ebx=ptr{int from;int len;char data[56];}
    if (eax == SYS_MQ_RECV) {
        if (!is_user_ptr(ebx)) return 0;
        typedef struct { int from; int len; char data[MQ_MSG_SIZE]; } MqRes;
        MqRes *r = (MqRes*)ebx;
        int from = -1;
        int n = mq_recv(task_get_current(), (uint8_t*)r->data, MQ_MSG_SIZE, &from);
        r->from = from;
        r->len  = n;
        return (uint64_t)(n > 0 ? 1 : 0);
    }

    // SYS_BRK(62): perluas user heap; ebx=new_end → return new_end aktual
    if (eax == SYS_BRK) {
        int tid = task_get_current();
        uint64_t cur_end = task_get_heap_end(tid);
        uint64_t new_end = ebx;
        if (new_end == 0) return cur_end;          /* query current brk */
        if (new_end <= cur_end) return cur_end;    /* no shrink */
        if (new_end > 0x5FE000ULL) return cur_end; /* jangan masuk guard page */
        /* Tier-1: tegakkan rlimit_mem (KB) jika diset via SYS_SETRLIMIT.
         * Heap dimulai dari 0x400000, jadi ukuran heap = new_end - 0x400000. */
        {
            uint32_t mem_kb = 0; uint16_t fds_unused = 0;
            task_get_rlimit(tid, &mem_kb, &fds_unused);
            if (mem_kb && (new_end - 0x400000ULL) > (uint64_t)mem_kb * 1024ULL)
                return cur_end;
        }
        /* Petakan frame baru untuk halaman yang belum dipetakan */
        uint64_t *pdir = task_get_page_dir(tid);
        uint64_t pg;
        for (pg = (cur_end & ~(uint64_t)0xFFF); pg < new_end; pg += 0x1000) {
            uint64_t frame = pmm_alloc_frame();
            if (!frame) break;
            vmm_map_page(pdir, pg, frame, 7);  /* P+RW+User */
            cur_end = pg + 0x1000;
        }
        task_set_heap_end(tid, cur_end);
        return cur_end;
    }

    // SYS_WAITPID(63): tunggu task; ebx=tid → return exit code
    if (eax == SYS_WAITPID) {
        int wtid = (int)ebx;
        task_wait(wtid);
        return (uint64_t)(int64_t)task_get_exit_code(wtid);
    }

    // SYS_THREAD_CREATE(64): buat thread; ebx=entry_va, edx=arg → return tid
    if (eax == SYS_THREAD_CREATE) {
        int parent = task_get_current();
        int tid = task_create_thread(ebx, edx, parent);
        return (uint64_t)(int64_t)tid;
    }

    // SYS_THREAD_EXIT(65): keluar dari thread saat ini
    if (eax == SYS_THREAD_EXIT) {
        task_exit();
        return 0; /* tidak dicapai */
    }

    // SYS_THREAD_JOIN(66): tunggu thread selesai; ebx=tid
    if (eax == SYS_THREAD_JOIN) {
        task_wait((int)ebx);
        return 0;
    }

    // SYS_THREAD_SET_NAME(67): set nama thread; ebx=tid, edx=name_ptr
    if (eax == SYS_THREAD_SET_NAME) {
        if (!is_user_ptr(edx)) return (uint64_t)-1;
        task_set_name((int)ebx, (const char *)edx);
        return 0;
    }

    // SYS_COND_ALLOC(68): alokasi condvar → return id
    if (eax == SYS_COND_ALLOC) {
        return (uint64_t)(int64_t)cv_alloc();
    }

    // SYS_COND_FREE(69): bebaskan condvar; ebx=id
    if (eax == SYS_COND_FREE) {
        cv_free((int)ebx);
        return 0;
    }

    // SYS_COND_WAIT(70): cv_wait; ebx=cond_id, edx=sem_id
    if (eax == SYS_COND_WAIT) {
        return (uint64_t)(int64_t)cv_wait((int)ebx, (int)edx);
    }

    // SYS_COND_SIGNAL(71): cv_signal; ebx=cond_id
    if (eax == SYS_COND_SIGNAL) {
        return (uint64_t)(int64_t)cv_signal((int)ebx);
    }

    // SYS_COND_BROADCAST(72): cv_broadcast; ebx=cond_id
    if (eax == SYS_COND_BROADCAST) {
        return (uint64_t)(int64_t)cv_broadcast((int)ebx);
    }

    // ---------------------------------------------------------------
    // Fondasi Q — Proses & Memori Lanjutan
    // ---------------------------------------------------------------

    // SYS_FORK(73): fork() via int 0x80.
    // Kernel membaca RIP dan RSP user langsung dari iretq frame di kernel stack induk.
    // Induk mendapat tid anak; anak melanjutkan eksekusi dari RIP user dengan rax=0.
    if (eax == SYS_FORK) {
        int parent = task_get_current();
        uint64_t *parent_pml4 = task_get_page_dir(parent);
        if (!parent_pml4) return (uint64_t)-1;

        /* Baca RIP dan RSP user dari frame iretq di kernel stack induk.
         * Layout kernel stack saat int80_handler + call syscall_handler:
         *   kstack_top-8   = SS user
         *   kstack_top-16  = RSP user   ← kita ambil
         *   kstack_top-24  = RFLAGS
         *   kstack_top-32  = CS user
         *   kstack_top-40  = RIP user   ← kita ambil (= titik resume setelah int $0x80)
         */
        uint64_t kstack_top = task_get_rsp0(parent);
        uint64_t child_rip  = *(uint64_t *)(kstack_top - 40);
        uint64_t child_rsp  = *(uint64_t *)(kstack_top - 16);

        if (!is_user_ptr(child_rip) || !is_user_ptr(child_rsp))
            return (uint64_t)-1;

        /* Buat PML4 anak */
        uint64_t *child_pml4 = vmm_create_page_dir();
        if (!child_pml4) return (uint64_t)-1;

        /* Setup COW antara parent dan child */
        vmm_copy_cow(parent_pml4, child_pml4);

        /* Buat task anak */
        int child = task_create_fork(parent, child_rip, child_rsp);
        if (child < 0) { vmm_free_user_memory(child_pml4); return (uint64_t)-1; }

        /* Pasang PML4 anak */
        task_set_page_dir(child, child_pml4);

        return (uint64_t)(int64_t)child;  /* induk mendapat tid anak */
    }

    // SYS_EXEC_REPLACE(74): ganti image proses; ebx=ptr nama file
    // Tidak pernah kembali ke pemanggil — langsung iretq ke entry baru.
    if (eax == SYS_EXEC_REPLACE) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        const char *name = (const char *)ebx;
        uint32_t sz = 0;
        const uint8_t *data = fs_read_bin(name, &sz);
        if (!data || sz == 0) return (uint64_t)-1;

        /* Muat ELF ke PML4 baru */
        uint64_t *new_dir = vmm_create_page_dir();
        if (!new_dir) return (uint64_t)-1;
        uint64_t entry = elf_load(data, sz, new_dir);
        if (!entry) { vmm_free_user_memory(new_dir); return (uint64_t)-1; }

        /* Alokasikan stack user baru di 0x600000 */
        uint64_t stack_frame = pmm_alloc_frame();
        if (!stack_frame) { vmm_free_user_memory(new_dir); return (uint64_t)-1; }
        vmm_map_page(new_dir, 0x600000ULL, stack_frame, 7);

        int tid = task_get_current();
        uint64_t *old_dir = task_get_page_dir(tid);

        /* Reset state task */
        task_set_page_dir(tid, new_dir);
        task_set_heap_end(tid, 0x600000ULL); /* heap kosong, di bawah stack */
        vfs_close_all(tid);
        vfs_init_task(tid);

        /* Switch ke PML4 baru dan bebaskan PML4 lama */
        vmm_switch_dir(new_dir);
        vmm_free_user_memory(old_dir);

        /* Lompat langsung ke user space lewat iretq — tidak kembali dari syscall */
        __asm__ volatile (
            "cli\n\t"
            "push $0x23\n\t"        /* SS: user data  */
            "push %1\n\t"           /* RSP: user stack */
            "push $0x202\n\t"       /* RFLAGS: IF=1   */
            "push $0x2B\n\t"        /* CS: user code  */
            "push %0\n\t"           /* RIP: entry     */
            "iretq\n\t"
            :: "r"(entry), "r"((uint64_t)0x601000)
            : "memory"
        );
        __builtin_unreachable();
    }

    // SYS_MMAP(75): alloc N halaman anonim; ebx=n_pages → return VA awal, 0=gagal
    if (eax == SYS_MMAP) {
        uint64_t n = ebx;
        if (n == 0 || n > 256) return 0;   /* batasi 1MB per mmap */
        int tid = task_get_current();
        uint64_t *pdir = task_get_page_dir(tid);
        if (!pdir) return 0;
        /* VA bump allocator mulai dari 0x900000, batas atas 0xB00000 (region mmap_file) */
        static uint64_t mmap_next_va = 0x900000ULL;
        if (mmap_next_va + n * 0x1000ULL > 0xB00000ULL) return 0;
        uint64_t va_base = mmap_next_va;
        uint64_t i;
        for (i = 0; i < n; i++) {
            uint64_t frame = pmm_alloc_frame();
            if (!frame) {
                /* rollback: unmap yang sudah dipetakan */
                uint64_t j;
                for (j = 0; j < i; j++) {
                    uint64_t va = va_base + j * 0x1000ULL;
                    uint64_t phys = vmm_get_phys(pdir, va);
                    vmm_unmap_page(pdir, va);
                    if (phys >= 768ULL * 4096) pmm_free_frame(phys);
                }
                return 0;
            }
            /* zero-fill frame */
            uint8_t *fp = (uint8_t *)frame;
            uint32_t b;
            for (b = 0; b < 4096; b++) fp[b] = 0;
            vmm_map_page(pdir, va_base + i * 0x1000ULL, frame, 7);
        }
        mmap_next_va += n * 0x1000ULL;
        return va_base;
    }

    // SYS_MUNMAP(76): bebaskan mapping; ebx=va, edx=n_pages
    if (eax == SYS_MUNMAP) {
        uint64_t va   = ebx & ~(uint64_t)0xFFF;
        uint64_t n    = edx;
        if (n == 0 || va < 0x900000ULL) return 0;
        int tid = task_get_current();
        uint64_t *pdir = task_get_page_dir(tid);
        if (!pdir) return 0;
        uint64_t i;
        for (i = 0; i < n; i++) {
            uint64_t page_va = va + i * 0x1000ULL;
            uint64_t phys = vmm_get_phys(pdir, page_va);
            vmm_unmap_page(pdir, page_va);
            if (phys >= 768ULL * 4096) pmm_free_frame(phys);
        }
        return 0;
    }

    // ---------------------------------------------------------------
    // Fondasi AX — file-backed mmap / munmap
    // ---------------------------------------------------------------

    // SYS_MMAP_FILE(98): mmap file dari fd.
    // ebx=fd, edx=n_pages (0=auto dari ukuran file) → VA awal, 0=gagal
    if (eax == SYS_MMAP_FILE) {
        int fd = (int)ebx;
        int tid = task_get_current();
        uint64_t *pdir = task_get_page_dir(tid);
        if (!pdir) return 0;

        /* Seek ke awal, lalu baca sampai EOF */
        vfs_seek(tid, fd, 0);

        static uint64_t mmap_file_next_va = 0xB00000ULL;  /* terpisah dari anonim */
        uint64_t va_base = mmap_file_next_va;

        /* Baca file 4096 byte per halaman; stop saat vfs_read < 4096 */
        uint64_t max_pages = (uint64_t)edx;
        if (max_pages == 0) max_pages = 256;  /* auto: max 1MB */
        if (max_pages > 256) max_pages = 256;

        uint64_t npages = 0;
        uint64_t i;
        for (i = 0; i < max_pages; i++) {
            uint64_t frame = pmm_alloc_frame();
            if (!frame) break;
            uint8_t *fp = (uint8_t *)frame;
            /* zero-fill */
            uint32_t b; for (b = 0; b < 4096; b++) fp[b] = 0;
            /* Baca satu halaman dari file */
            int nr = vfs_read(tid, fd, (char *)fp, 4096);
            vmm_map_page(pdir, va_base + i * 0x1000ULL, frame, 7); /* P+RW+User */
            npages++;
            if (nr < 4096) break;  /* EOF */
        }
        if (npages == 0) return 0;
        mmap_file_next_va += (npages + 1) * 0x1000ULL;  /* +1 guard page */
        return va_base;
    }

    // SYS_MUNMAP_FILE(99): bebaskan file-backed mapping; sama seperti MUNMAP
    if (eax == SYS_MUNMAP_FILE) {
        uint64_t va = ebx & ~(uint64_t)0xFFF;
        uint64_t n  = edx;
        if (n == 0) return 0;
        int tid = task_get_current();
        uint64_t *pdir = task_get_page_dir(tid);
        if (!pdir) return 0;
        uint64_t i;
        for (i = 0; i < n; i++) {
            uint64_t page_va = va + i * 0x1000ULL;
            uint64_t phys = vmm_get_phys(pdir, page_va);
            vmm_unmap_page(pdir, page_va);
            if (phys >= 768ULL * 4096) pmm_free_frame(phys);
        }
        return 0;
    }

    // ---------------------------------------------------------------
    // F-R2 — VFS pipe/net/tty via file descriptor
    // ---------------------------------------------------------------

    // SYS_PIPE2(77): buat pipe, kembalikan dua fd di array int[2].
    // ebx = ptr int[2] { fd_read, fd_write }
    if (eax == SYS_PIPE2) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        int *fds = (int *)ebx;
        int fd_r = -1, fd_w = -1;
        int tid = task_get_current();
        int r = vfs_pipe(tid, &fd_r, &fd_w);
        fds[0] = fd_r;
        fds[1] = fd_w;
        return (uint64_t)(int64_t)r;
    }

    // SYS_NET_OPEN(78): buka TCP socket ke ip:port, kembalikan fd.
    // ebx = ptr struct { uint8_t ip[4]; uint16_t port; }
    if (eax == SYS_NET_OPEN) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        typedef struct { uint8_t ip[4]; uint16_t port; } NetArgs;
        NetArgs *a = (NetArgs *)ebx;
        int tid = task_get_current();
        return (uint64_t)(int64_t)vfs_net_open(tid, a->ip, a->port);
    }

    // SYS_TTY_OPEN(79): alokasi TTY fd untuk task saat ini.
    if (eax == SYS_TTY_OPEN) {
        int tid = task_get_current();
        return (uint64_t)(int64_t)vfs_tty_open(tid);
    }

    // SYS_PIPE_REDIRECT(80): redirect stdout/stdin ke pipe.
    // ebx = target_tid, edx = (dir<<24)|pipe_id
    //   dir=0 → redirect stdin  (fd 0) ke read-end pipe
    //   dir=1 → redirect stdout (fd 1) ke write-end pipe
    if (eax == SYS_PIPE_REDIRECT) {
        int target    = (int)ebx;
        int pipe_id   = (int)(edx & 0xFFFFFF);
        int dir       = (int)(edx >> 24);
        if (dir == 1)
            return (uint64_t)(int64_t)vfs_redirect_out_pipe(target, pipe_id);
        else
            return (uint64_t)(int64_t)vfs_redirect_in_pipe(target, pipe_id);
    }

    // ---------------------------------------------------------------
    // F-R3 — MFS4 inode layer syscalls
    // ---------------------------------------------------------------

    // SYS_MFS4_SYMLINK(81): buat symlink; ebx=link_path_ptr, edx=target_path_ptr
    if (eax == SYS_MFS4_SYMLINK) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_symlink((const char*)ebx, (const char*)edx);
    }

    // SYS_MFS4_HARDLINK(82): buat hardlink; ebx=link_path_ptr, edx=orig_path_ptr
    if (eax == SYS_MFS4_HARDLINK) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_hardlink((const char*)ebx, (const char*)edx);
    }

    // SYS_MFS4_STAT(83): stat file; ebx=path_ptr, edx=ptr MFS4Stat
    if (eax == SYS_MFS4_STAT) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_stat((const char*)ebx, (MFS4Stat*)edx);
    }

    // SYS_MFS4_LISTDIR(84): list dir; ebx=dir_ptr, edx=ptr{char*buf, int bufsz}
    if (eax == SYS_MFS4_LISTDIR) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return (uint64_t)-1;
        typedef struct { char *buf; int bufsz; } LDArgs;
        LDArgs *a = (LDArgs*)edx;
        if (!is_user_ptr((uint64_t)a->buf)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_listdir((const char*)ebx, a->buf, a->bufsz);
    }

    // SYS_MFS4_UNLINK(85): unlink; ebx=path_ptr
    if (eax == SYS_MFS4_UNLINK) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_unlink((const char*)ebx);
    }

    // SYS_MFS4_MKDIR(86): mkdir via MFS4; ebx=path_ptr
    if (eax == SYS_MFS4_MKDIR) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_mkdir((const char*)ebx);
    }

    // SYS_SIGACTION(87): daftarkan handler sinyal (stub — hanya terima, belum dipakai)
    if (eax == SYS_SIGACTION) {
        return 0;  /* stub: handler belum didelivery ke user space */
    }

    // SYS_SIGKILL_SIG(88): kirim sinyal ke task: ebx=tid, edx=sig
    if (eax == SYS_SIGKILL_SIG) {
        task_send_signal((int)ebx, (int)edx);
        return 0;
    }

    // SYS_FUTEX_WAIT(89): jika *(int*)ebx == (int)edx → masuk futex_table, blok, return 0
    //                     jika nilai sudah berubah → return -1 (tidak blok)
    if (eax == SYS_FUTEX_WAIT) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        // Baca nilai atomik dari user space
        int cur = *(volatile int *)(uint64_t)ebx;
        if (cur != (int)edx) return (uint64_t)-1;  // nilai sudah lain, tidak perlu tunggu

        // Daftarkan ke futex_table
        int slot = -1, fi;
        for (fi = 0; fi < FUTEX_TABLE_SIZE; fi++) {
            if (!futex_table[fi].used) { slot = fi; break; }
        }
        if (slot == -1) return (uint64_t)-1;  // table penuh

        int cur_tid = task_get_current();
        futex_table[slot].addr = (uint64_t)ebx;
        futex_table[slot].tid  = cur_tid;
        futex_table[slot].used = 1;

        task_block();  // blok sampai SYS_FUTEX_WAKE membangunkan
        return 0;
    }

    // SYS_FUTEX_WAKE(90): bangunkan maks (int)edx waiter yang menunggu di addr ebx
    if (eax == SYS_FUTEX_WAKE) {
        int n_wake = (int)edx;
        int count  = 0, fi;
        for (fi = 0; fi < FUTEX_TABLE_SIZE && count < n_wake; fi++) {
            if (futex_table[fi].used && futex_table[fi].addr == (uint64_t)ebx) {
                int wtid = futex_table[fi].tid;
                futex_table[fi].used = 0;
                futex_table[fi].addr = 0;
                task_unblock(wtid);
                count++;
            }
        }
        return (uint64_t)(uint32_t)count;
    }

    // SYS_GET_TLS(91): kembalikan VA halaman TLS task saat ini
    if (eax == SYS_GET_TLS) {
        int cur = task_get_current();
        return 0x800000ULL + (uint64_t)(uint32_t)cur * 0x1000ULL;
    }

    // SYS_MFS4_RENAME(92): rename inode MFS4; ebx=old_path, edx=new_path → 0/-1
    if (eax == SYS_MFS4_RENAME) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_rename((const char*)ebx, (const char*)edx);
    }

    // SYS_FCNTL(93): set fd flags; ebx=fd, edx=new_flags → 0/-1
    if (eax == SYS_FCNTL) {
        int fd   = (int)(int64_t)ebx;
        uint8_t new_flags = (uint8_t)(uint64_t)edx;
        int tid = task_get_current();
        return (uint64_t)(int64_t)vfs_set_flags(tid, fd, new_flags);
    }

    // SYS_POLL(94): poll array fd dengan timeout
    // ebx = pointer ke struct { KPollFd *fds; int nfds; int timeout_ms; }
    if (eax == SYS_POLL) {
        typedef struct { uint64_t fds_ptr; int nfds; int timeout_ms; } _PollArgs;
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        _PollArgs *a = (_PollArgs*)ebx;
        if (!is_user_ptr(a->fds_ptr)) return (uint64_t)-1;
        KPollFd *fds = (KPollFd*)a->fds_ptr;
        int nfds       = a->nfds;
        int timeout_ms = a->timeout_ms;
        if (nfds <= 0 || nfds > 16) return (uint64_t)-1;
        int tid = task_get_current();
        uint32_t deadline = get_ticks() + (uint32_t)(timeout_ms > 0 ? timeout_ms : 0);

        while (1) {
            int ready = 0;
            int i;
            for (i = 0; i < nfds; i++) {
                fds[i].revents = (short)vfs_fd_ready(tid, fds[i].fd, fds[i].events);
                if (fds[i].revents) ready++;
            }
            if (ready > 0)
                return (uint64_t)(int64_t)ready;
            if (timeout_ms == 0 || get_ticks() >= deadline)
                return 0;
            task_sleep(1);  /* yield 1 ms, coba lagi */
        }
    }

    /* Fondasi AA — SYS_GETARGV(95): kembalikan argc, isi buf + argv[] pointers.
     * rdi = char *buf       (user buffer, menerima null-terminated strings "arg0\0arg1\0...")
     * rsi = char **argv_out (user pointer array, akan diisi &buf[offset_k] per arg)
     * return = argc */
    if (eax == SYS_GETARGV) {
        char *ubuf     = (char *)(uintptr_t)ebx;
        char **uargv   = (char **)(uintptr_t)edx;
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return 0;
        int tid = task_get_current();
        int ac  = tasks_getargc(tid);
        /* Defensif: uargv di user-space diasumsikan punya 9 slot (argv[0..7] + NULL).
         * ac sudah dibatasi <=8 di task.c, tapi clamp di sini agar penulisan
         * uargv[ac] selalu di dalam batas walau batas itu berubah nanti. */
        if (ac > 8) ac = 8;
        const char *src = tasks_getargbuf(tid);  /* pointer ke kernel arg_buf */
        /* Salin arg_buf ke user buffer (max 512 byte) */
        int i;
        for (i = 0; i < 512; i++) {
            ubuf[i] = src[i];
            /* Dua null berturut = akhir serial */
            if (i > 0 && src[i-1] == '\0' && src[i] == '\0') { i++; break; }
        }
        ubuf[511] = '\0';
        /* Bangun pointer array — arahkan ke substring di ubuf */
        char *p = ubuf;
        for (i = 0; i < ac; i++) {
            uargv[i] = p;
            while (*p) p++;
            p++;  /* lewati null */
        }
        uargv[ac] = 0;  /* null sentinel */
        return (uint64_t)(unsigned int)ac;
    }

    /* AT4 — SYS_CLIP_COPY(96): salin string user ke clipboard kernel */
    if (eax == SYS_CLIP_COPY) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        clip_copy((const char *)(uintptr_t)ebx);
        return 0;
    }

    /* AT4 — SYS_CLIP_PASTE(97): baca clipboard ke buffer user */
    if (eax == SYS_CLIP_PASTE) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        int mlen = (int)(unsigned int)edx;
        if (mlen <= 0 || mlen > 512) mlen = 512;
        return (uint64_t)(unsigned int)clip_paste((char *)(uintptr_t)ebx, mlen);
    }

    // ---------------------------------------------------------------
    // Ekstensi B — syscall baru 100-105
    // ---------------------------------------------------------------

    // SYS_LSEEK(100): pindah posisi fd: ebx=fd, edx=offset (int32) → new offset / -1
    if (eax == SYS_LSEEK) {
        int fd     = (int)(unsigned int)ebx;
        int offset = (int)edx;
        int tid = task_get_current();
        int r = vfs_seek(tid, fd, offset);
        return (r == 0) ? (uint64_t)(uint32_t)offset : (uint64_t)-1;
    }

    // SYS_NET_LISTEN(101): mulai listen TCP: ebx=port → listen_id / -1
    if (eax == SYS_NET_LISTEN) {
        uint16_t port = (uint16_t)(ebx & 0xFFFF);
        return (uint64_t)(int)net_tcp_listen(port);
    }

    // SYS_NET_ACCEPT(102): terima koneksi: ebx=listen_id, edx=timeout_ms → conn_id / -1
    if (eax == SYS_NET_ACCEPT) {
        int listen_id   = (int)(unsigned int)ebx;
        uint32_t tmo_ms = (uint32_t)edx;
        return (uint64_t)(int)net_tcp_accept(listen_id, tmo_ms);
    }

    // SYS_NET_UNLISTEN(103): berhenti listen: ebx=listen_id
    if (eax == SYS_NET_UNLISTEN) {
        net_tcp_unlisten((int)(unsigned int)ebx);
        return 0;
    }

    // SYS_SETITIMER(104): set interval timer: ebx=interval_ms (0=off)
    if (eax == SYS_SETITIMER) {
        int tid = task_get_current();
        uint32_t interval = (uint32_t)ebx;
        task_set_itimer(tid, interval);
        return 0;
    }

    // SYS_GFX_FLIP(105): flush back buffer ke hardware framebuffer
    if (eax == SYS_GFX_FLIP) {
        gfx_flip();
        return 0;
    }

    // SYS_TIME(106): kembalikan detik sejak boot (get_ticks() / 1000)
    if (eax == SYS_TIME) {
        uint32_t secs = get_ticks() / 1000;
        if (ebx && is_user_ptr(ebx))
            *(uint32_t *)(uintptr_t)ebx = secs;
        return (uint64_t)secs;
    }

    // SYS_SIGPROCMASK(107): ebx=how (SIG_BLOCK/UNBLOCK/SETMASK), edx=mask → old_mask
    if (eax == SYS_SIGPROCMASK) {
        int tid = task_get_current();
        uint32_t old  = task_get_signal_mask(tid);
        uint32_t mask = (uint32_t)edx;
        int how = (int)ebx;
        if (how == SIG_BLOCK)        task_set_signal_mask(tid, old | mask);
        else if (how == SIG_UNBLOCK) task_set_signal_mask(tid, old & ~mask);
        else                         task_set_signal_mask(tid, mask);  /* SIG_SETMASK */
        return (uint64_t)old;
    }

    // SYS_SETRLIMIT(108): ebx=ptr{uint32_t mem_kb; uint16_t fds} → 0
    if (eax == SYS_SETRLIMIT) {
        if (!ebx || !is_user_ptr(ebx)) return (uint64_t)-1;
        typedef struct { uint32_t mem_kb; uint16_t fds; } RLArgs;
        RLArgs *a = (RLArgs *)(uintptr_t)ebx;
        task_set_rlimit(task_get_current(), a->mem_kb, a->fds);
        return 0;
    }

    // SYS_GETRLIMIT(109): ebx=ptr{uint32_t mem_kb; uint16_t fds} (output) → 0
    if (eax == SYS_GETRLIMIT) {
        if (!ebx || !is_user_ptr(ebx)) return (uint64_t)-1;
        typedef struct { uint32_t mem_kb; uint16_t fds; } RLArgs;
        RLArgs *a = (RLArgs *)(uintptr_t)ebx;
        task_get_rlimit(task_get_current(), &a->mem_kb, &a->fds);
        return 0;
    }

    return (uint64_t)-1; //kembalikan -1 untuk menandakan syscall tidak dikenal
}

/* Jalankan program dari FS langsung dari kernel (dipakai taskbar quick-launch) */
int kernel_exec(const char *name) {
    uint32_t sz = 0;
    const uint8_t *data = fs_read_bin(name, &sz);
    if (!data || sz == 0) return -1;
    uint64_t *proc_dir = vmm_create_page_dir();
    uint64_t entry = elf_load(data, sz, proc_dir);
    if (!entry) return -1;
    uint64_t stack_phys = pmm_alloc_frame();
    vmm_map_page(proc_dir, 0x600000, stack_phys, 7);
    return task_create_user(entry, proc_dir, 0x600000 + 0x1000, name, 0, 0);
}
