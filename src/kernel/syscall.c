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

extern void print(const char *str); // dari kernel.c
extern void clear_screen();         // dari kernel.c

/* Validasi pointer dari user space: tolak NULL dan pointer ke kernel space (<0x300000).
 * User programs dimuat di 0x300000+. Stack user di 0x400000+. */
static int is_user_ptr(uint32_t ptr) {
    return (ptr != 0 && ptr >= 0x300000u);
}

uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t edx) {
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

    // SYS_FS_READ(5): ebx = pointer nama file
    // return: pointer ke isi file (string), atau 0 jika tidak ditemukan
    if (eax == SYS_FS_READ) {
        if (!is_user_ptr(ebx)) return 0;
        const char *data = fs_read((const char*)ebx);
        return (uint32_t)data;
    }
    // SYS_FS_WRITE(6): ebx = pointer ke struct { const char *name; const char *data; }
    // return: 1 sukses, 0 gagal
    if (eax == SYS_FS_WRITE) {
        if (!is_user_ptr(ebx)) return 0;
        const char **args = (const char**)ebx;
        if (!is_user_ptr((uint32_t)args[0]) || !is_user_ptr((uint32_t)args[1])) return 0;
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

    // SYS_DRAW_PIXEL(22): ebx=x, edx=(y<<16)|color
    // y dikemas di bit 16–31 (mendukung 0–479), color di bit 0–7
    if (eax == SYS_DRAW_PIXEL) {
        int x = (int)ebx;
        int y = (int)((edx >> 16) & 0xFFFF);
        uint8_t color = (uint8_t)(edx & 0xFF);
        draw_pixel(x, y, color);
        return 0;
    }
    // SYS_FILL_SCREEN(23): ebx=color — isi seluruh layar
    if (eax == SYS_FILL_SCREEN) {
        fill_screen((uint8_t)ebx);
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

    return (uint32_t)-1; //kembalikan -1 untuk menandakan syscall tidak dikenal
}
