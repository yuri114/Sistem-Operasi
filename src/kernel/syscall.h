#ifndef SYSCALL_H
#define SYSCALL_H
#include <stdint.h>

#define SYS_PRINT    0
#define SYS_GETKEY   1
#define SYS_EXIT     2
#define SYS_ALLOC    3
#define SYS_FREE     4
#define SYS_FS_READ  5
#define SYS_FS_WRITE 6
#define SYS_MSG_SEND 7
#define SYS_MSG_RECV 8
#define SYS_KILL     9

void syscall_init(); //fungsi untuk inisialisasi sistem panggilan, seperti mendaftarkan handler di IDT
uint32_t syscall_handler(uint32_t eax, uint32_t ebx); //fungsi untuk menangani panggilan sistem, eax = nomor syscall, ebx = argumen (jika ada)

#endif