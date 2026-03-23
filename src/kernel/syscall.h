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
#define SYS_KILL      9
#define SYS_SEM_ALLOC 10  // alokasi semaphore baru, ebx=initial_value, return id
#define SYS_SEM_FREE  11  // bebaskan semaphore, ebx=id
#define SYS_SEM_WAIT  12  // sem_wait, ebx=id
#define SYS_SEM_POST  13  // sem_post, ebx=id
#define SYS_PIPE_OPEN  14  // alokasi pipe baru, return id
#define SYS_PIPE_WRITE 15  // tulis ke pipe: ebx=id, edx=str_ptr
#define SYS_PIPE_READ  16  // baca dari pipe: ebx=id, edx=buf_ptr
#define SYS_PIPE_CLOSE 17  // tutup pipe: ebx=id
#define SYS_PIPE_GETID 18  // ambil pipe_id task saat ini (untuk inherited pipe)

void syscall_init();
uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t edx);

#endif