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
#define SYS_DEV_WRITE  19  // dev_write: ebx=dev_id, edx=str_ptr
#define SYS_DEV_READ   20  // dev_read:  ebx=dev_id, edx=buf_ptr
#define SYS_DEV_IOCTL  21  // dev_ioctl: ebx=dev_id, edx=cmd<<16|arg

// Syscall grafis (Phase 24)
#define SYS_DRAW_PIXEL  22  // ebx=x, edx=(y<<8)|color — gambar 1 piksel
#define SYS_FILL_SCREEN 23  // ebx=color — isi seluruh layar
#define SYS_FILL_RECT   24  // ebx=ptr GfxRect — gambar persegi panjang
#define SYS_DRAW_LINE   25  // ebx=ptr GfxLine — gambar garis
#define SYS_CLR_SCREEN  26  // kosongkan layar + reset kursor

#define SYS_GETPID 27  // kembalikan id task saat ini
#define SYS_YIELD  28  // lepas sisa slot CPU ke task lain
#define SYS_SLEEP  29  // tidur ebx milidetik
#define SYS_EXEC   30  // jalankan program dari FS: ebx=nama, return task_id atau -1

void syscall_init();
uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t edx);

#endif