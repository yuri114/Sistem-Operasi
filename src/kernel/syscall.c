#include "syscall.h"
#include "keyboard.h"
#include "task.h"
#include "memory.h"

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

    return (uint32_t)-1; //kembalikan -1 untuk menandakan syscall tidak dikenal
}
