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
#define SYS_FS_SYNC        49 // flush semua dirty file ke disk → return jumlah file di-flush
#define SYS_FS_TMPWRITE    50 // tulis data ke tmpfs: ebx=nama_ptr, edx=ptr FSWriteArgs
#define SYS_FS_MKDIR       51 // buat direktori: ebx=nama_ptr → return 1/0
#define SYS_PIPE_NAMED     52 // buka/buat named pipe: ebx=nama_ptr → return pipe_id
#define SYS_SHM_CREATE     53 // buat shared memory: ebx=key_ptr → return shm_id
#define SYS_SHM_ATTACH     54 // map shm ke proses: ebx=shm_id → return VA
#define SYS_SHM_DETACH     55 // lepas shm dari proses: ebx=shm_id

/* Tahap J — VFS (file descriptor) */
#define SYS_OPEN      56  // buka/buat file: ebx=path_ptr, edx=flags → return fd
#define SYS_READ_FD   57  // baca fd: ebx=fd, edx=ptr VfsRWArgs → return bytes
#define SYS_WRITE_FD  58  // tulis fd: ebx=fd, edx=ptr VfsRWArgs → return bytes
#define SYS_CLOSE_FD  59  // tutup fd: ebx=fd → return 0/-1

/* Tahap L — Message Queue */
#define SYS_MQ_SEND   60  // kirim MQ: ebx=dst_pid, edx=str_ptr (max 56 char)
#define SYS_MQ_RECV   61  // terima MQ: ebx=ptr MqRecvResult, edx=unused → return 1/0

/* Fondasi: user heap + waitpid */
#define SYS_BRK       62  // perluas user heap: ebx=new_end → return new_end sesungguhnya
#define SYS_WAITPID   63  // tunggu task selesai: ebx=tid → return 0

/* Tahap N — User-space Threading */
#define SYS_THREAD_CREATE 64  // buat thread: ebx=entry_va, edx=arg → return tid
#define SYS_THREAD_EXIT   65  // keluar dari thread saat ini → tidak return
#define SYS_THREAD_JOIN   66  // tunggu thread selesai: ebx=tid → return 0

/* Fondasi P — Thread Naming + Condition Variable */
#define SYS_THREAD_SET_NAME 67  // set nama thread: ebx=tid, edx=name_ptr
#define SYS_COND_ALLOC      68  // alokasi condvar → return id
#define SYS_COND_FREE       69  // bebaskan condvar: ebx=id
#define SYS_COND_WAIT       70  // cv_wait: ebx=cond_id, edx=sem_id
#define SYS_COND_SIGNAL     71  // cv_signal: ebx=cond_id
#define SYS_COND_BROADCAST  72  // cv_broadcast: ebx=cond_id

/* Fondasi Q — Proses & Memori Lanjutan */
#define SYS_FORK          73  // fork(); ebx=child_rip, edx=child_rsp → 0=child, tid=parent
#define SYS_EXEC_REPLACE  74  // ganti image proses: ebx=nama_ptr → tidak return ke caller
#define SYS_MMAP          75  // alloc N halaman anonim: ebx=n_pages → VA, atau 0 gagal
#define SYS_MUNMAP        76  // bebaskan: ebx=va, edx=n_pages

/* F-R2 — VFS pipe/net/tty via fd */
#define SYS_PIPE2         77  // buat pipe: ebx=ptr int[2] {fd_r,fd_w} → 0/-1
#define SYS_NET_OPEN      78  // buka TCP: ebx=ptr{u8 ip[4];u16 port} → fd
#define SYS_TTY_OPEN      79  // alokasi TTY fd → fd atau -1
#define SYS_PIPE_REDIRECT 80  // redirect stdout/stdin ke pipe: ebx=tid,edx=pipe_id|dir<<24

/* F-R3 — MFS4 inode layer */
#define SYS_MFS4_SYMLINK  81  // buat symlink: ebx=link_ptr, edx=target_ptr → 0/-1
#define SYS_MFS4_HARDLINK 82  // buat hardlink: ebx=link_ptr, edx=orig_ptr → 0/-1
#define SYS_MFS4_STAT     83  // stat: ebx=path_ptr, edx=ptr MFS4Stat → 0/-1
#define SYS_MFS4_LISTDIR  84  // list dir: ebx=dir_ptr, edx=ptr{char*,int} → count
#define SYS_MFS4_UNLINK   85  // unlink: ebx=path_ptr → 0/-1
#define SYS_MFS4_MKDIR    86  // mkdir: ebx=path_ptr → 0/-1

/* F-T — Signal & Process Control */
#define SYS_SIGACTION     87  // daftarkan handler sinyal: ebx=sig, edx=handler_va → 0
#define SYS_SIGKILL_SIG   88  // kirim sinyal: ebx=tid, edx=sig → 0

/* F-U — Futex + TLS */
#define SYS_FUTEX_WAIT    89  // futex wait: ebx=addr, edx=expected → 0=dibangunkan, -1=mismatch
#define SYS_FUTEX_WAKE    90  // futex wake: ebx=addr, edx=n_wake → jumlah yang dibangunkan
#define SYS_GET_TLS       91  // get TLS base: → VA halaman TLS task ini (0x800000+tid*0x1000)

/* F-V — MFS4 Disk Persistence + Rename */
#define SYS_MFS4_RENAME   92  // rename: ebx=old_path_ptr, edx=new_path_ptr → 0/-1

/* F-W — Non-blocking fd + poll() */
#define SYS_FCNTL         93  // set fd flags: ebx=fd, edx=new_flags → 0/-1
#define SYS_POLL          94  // poll: ebx=KPollArgs* → jumlah fd siap / 0=timeout / -1=error

/* Fondasi AA — argv retrieval */
#define SYS_GETARGV       95  // get argv: rdi=buf_ptr, rsi=argv_ptr_array → return argc

void syscall_init();
uint64_t syscall_handler(uint64_t eax, uint64_t ebx, uint64_t edx);

/* Jalankan program dari FS langsung dari kernel (tanpa user syscall) */
int kernel_exec(const char *name);

#endif