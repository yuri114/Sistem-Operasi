#include "syscall.h"
#include "keyboard.h"
#include "task.h"
#include "memory.h"
#include "fs.h"
#include "ipc.h"

extern void print(const char *str); // dari kernel.c

uint32_t syscall_handler(uint32_t eax, uint32_t ebx) {
    if (eax == SYS_PRINT){
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
        const char *data = fs_read((const char*)ebx);
        return (uint32_t)data;
    }
    // SYS_FS_WRITE(6): ebx = pointer ke struct { const char *name; const char *data; }
    // return: 1 sukses, 0 gagal
    if (eax == SYS_FS_WRITE) {
        // struct dikirim lewat pointer di ebx
        const char **args = (const char**)ebx;
        const char *name = args[0];
        const char *data = args[1];
        return (uint32_t)fs_write(name, data);
    }

    // SYS_MSG_SEND(7): ebx = pointer string pesan
    // return: 1 sukses, 0 queue penuh
    if (eax == SYS_MSG_SEND) {
        return (uint32_t)ipc_send((const char*)ebx);
    }
    // SYS_MSG_RECV(8): ebx = pointer buffer tujuan (minimal 64 byte)
    // return: 1 ada pesan, 0 queue kosong
    if (eax == SYS_MSG_RECV) {
        return (uint32_t)ipc_recv((char*)ebx);
    }

    // SYS_KILL(9): ebx = id proses yang akan dimatikan
    // return: 1 sukses, 0 gagal (id tidak valid / dilindungi)
    if (eax == SYS_KILL) {
        return (uint32_t)task_kill((int)ebx);
    }

    return (uint32_t)-1; //kembalikan -1 untuk menandakan syscall tidak dikenal
}
