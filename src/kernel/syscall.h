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

// Syscall grafis teks dan mouse
#define SYS_DRAW_CHAR  31  // gambar 1 karakter di framebuffer: ebx=x|(y<<16), edx=c|(fg<<8)|(bg<<16)
#define SYS_DRAW_STR   32  // gambar string di framebuffer: ebx=ptr GfxStr
#define SYS_MOUSE_GET  33  // baca state mouse: ebx=ptr MouseState (output)

// Window Manager syscalls
#define SYS_WIN_CREATE  34  // buat window: ebx=ptr WinCreateArgs → return id
#define SYS_WIN_DESTROY 35  // tutup window: ebx=id
#define SYS_WIN_DRAW    36  // gambar teks di konten: ebx=ptr WinDrawArgs
#define SYS_WIN_CLEAR   37  // bersihkan konten: ebx=id, edx=warna_bg
#define SYS_WIN_EVENT   38  // poll event: ebx=id → return WIN_EVENT_* (encode char/btn di byte atas)
#define SYS_WIN_BTN_ADD 39  // tambah tombol ke window: ebx=ptr WinBtnArgs → return btn_idx
#define SYS_WIN_CLICK_POS 40 // koordinat klik terakhir: ebx=id, edx=ptr int[2] → out[0]=x, out[1]=y
#define SYS_WIN_DRAW_PIXEL 41 // gambar piksel di konten window: ebx=id, edx=x|(y<<12)|((color&0xF)<<24)
#define SYS_WIN_FILL_RECT  42 // isi persegi di konten window: ebx=id, edx=ptr WinFillArgs {short x,y,w,h; u8 color}
#define SYS_WIN_MOUSE_REL  43 // posisi mouse relatif konten: ebx=id, edx=ptr int[3] → [rel_x, rel_y, btn]
#define SYS_WIN_MINIMIZE   44 // minimize window: ebx=id
#define SYS_WIN_RESTORE    45 // restore window dari minimized: ebx=id
#define SYS_FS_LIST        46 // list nama file ke buffer: ebx=ptr, edx=bufsz → return count
#define SYS_FS_DELETE      47 // hapus file: ebx=ptr nama → return 1/0
#define SYS_GET_TICKS      48 // kembalikan jumlah timer tick sejak boot → return uint32_t

void syscall_init();
uint64_t syscall_handler(uint64_t eax, uint64_t ebx, uint64_t edx);

/* Jalankan program dari FS langsung dari kernel (tanpa user syscall) */
int kernel_exec(const char *name);

#endif