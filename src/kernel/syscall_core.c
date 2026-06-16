#include "syscall.h"
#include "syscall_internal.h"
#include "keyboard.h"
#include "task.h"
#include "memory.h"
#include "vfs.h"
#include "timer.h"

extern void print(const char *str); // dari kernel.c

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

uint64_t syscall_dispatch_core(uint64_t eax, uint64_t ebx, uint64_t edx, int *handled) {
    *handled = 1;

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

    // SYS_GET_TICKS(48): kembalikan jumlah timer tick sejak boot
    if (eax == SYS_GET_TICKS) {
        return get_ticks();
    }

    // SYS_TIME(106): kembalikan detik sejak boot (get_ticks() / 1000)
    if (eax == SYS_TIME) {
        uint32_t secs = get_ticks() / 1000;
        if (ebx && is_user_ptr(ebx))
            *(uint32_t *)(uintptr_t)ebx = secs;
        return (uint64_t)secs;
    }

    // AT4 — SYS_CLIP_COPY(96): salin string user ke clipboard kernel
    if (eax == SYS_CLIP_COPY) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        clip_copy((const char *)(uintptr_t)ebx);
        return 0;
    }

    // AT4 — SYS_CLIP_PASTE(97): baca clipboard ke buffer user
    if (eax == SYS_CLIP_PASTE) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        int mlen = (int)(unsigned int)edx;
        if (mlen <= 0 || mlen > 512) mlen = 512;
        return (uint64_t)(unsigned int)clip_paste((char *)(uintptr_t)ebx, mlen);
    }

    *handled = 0;
    return 0;
}
