#include "syscall.h"
#include "keyboard.h"
#include "task.h"
#include "memory.h"
#include "fs.h"
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

extern void print(const char *str); // dari kernel.c
extern void clear_screen();         // dari kernel.c

/* Validasi pointer dari user space: tolak NULL dan pointer ke kernel space (<0x300000).
 * User programs dimuat di 0x300000+. Stack user di 0x400000+. */
static int is_user_ptr(uint64_t ptr) {
    return (ptr != 0 && ptr >= 0x300000ULL);
}

uint64_t syscall_handler(uint64_t eax, uint64_t ebx, uint64_t edx) {
    if (eax == SYS_PRINT){
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        print((const char*)ebx); //ebx berisi pointer ke string yang akan dicetak
        return 0; //kembalikan 0 untuk menandakan sukses
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
        task_exit(); //keluar dari task saat ini
        return 0; //tidak akan pernah sampai sini karena task_exit akan menghentikan task
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

    // SYS_EXEC(30): muat dan jalankan program dari FS: ebx=nama (user ptr)
    // return: task_id jika sukses, (uint32_t)-1 jika gagal
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
        int tid = task_create_user(entry, proc_dir, 0x600000 + 0x1000, name);
        return (uint64_t)tid;
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
    return task_create_user(entry, proc_dir, 0x600000 + 0x1000, name);
}
