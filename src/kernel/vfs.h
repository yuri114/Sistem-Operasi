/* vfs.h — Virtual Filesystem layer
 *
 * Abstraksi file descriptor per-task di atas MFS3/MFS4.
 * Setiap task punya tabel hingga VFS_MAX_FD descriptor.
 * fd=0: stdin (keyboard), fd=1: stdout (print), fd=2: stderr,
 * fd=3+: file/pipe/net/tty yang dibuka via vfs_open().
 */
#ifndef VFS_H
#define VFS_H
#include <stdint.h>

#define VFS_MAX_FD    32   /* max fd per task                         */
#define VFS_FD_STDIN  0    /* standard input  (keyboard)              */
#define VFS_FD_STDOUT 1    /* standard output (screen)                */
#define VFS_FD_STDERR 2    /* standard error  (= stdout for now)      */

/* Tipe fd */
#define VFS_TYPE_NONE   0
#define VFS_TYPE_STDIN  1
#define VFS_TYPE_STDOUT 2
#define VFS_TYPE_FILE   3  /* MFS3/MFS4 file dengan offset baca/tulis */
#define VFS_TYPE_PIPE   4  /* pipe — anonymous atau named             */
#define VFS_TYPE_NET    5  /* TCP socket via net_tcp_*                */
#define VFS_TYPE_TTY    6  /* pseudo-terminal (gui_term / pipe shell) */
#define VFS_TYPE_PROC   7  /* Fondasi AH: virtual /proc file         */
#define VFS_TYPE_DEV    8  /* virtual /dev: null, zero, random       */

/* Subtype untuk VFS_TYPE_DEV */
#define VFS_DEV_NULL    0
#define VFS_DEV_ZERO    1

/* Flag open */
#define VFS_O_RDONLY    0x01
#define VFS_O_WRONLY    0x02
#define VFS_O_RDWR      0x03
#define VFS_O_CREATE    0x04  /* buat file jika belum ada               */
#define VFS_O_NONBLOCK  0x08  /* non-blocking: baca return -EAGAIN jika kosong */

/* Kode error non-blocking */
#define VFS_EAGAIN  (-11)     /* -EAGAIN: operasi akan blok, coba lagi  */

/* F-W2: struct untuk poll() syscall (harus cocok dengan UPollFd di lib.h) */
typedef struct {
    int   fd;
    short events;    /* POLLIN=1, POLLOUT=2, POLLERR=4               */
    short revents;   /* filled by kernel                              */
} KPollFd;

#define POLLIN   1
#define POLLOUT  2
#define POLLERR  4

/* Descriptor per-fd */
typedef struct {
    uint8_t  type;        /* VFS_TYPE_*                               */
    uint8_t  used;        /* 1 = slot terpakai                        */
    uint8_t  flags;       /* VFS_O_*                                  */
    uint8_t  _pad;
    uint32_t offset;      /* posisi baca/tulis (TYPE_FILE)            */
    union {
        char  name[28];   /* nama file (TYPE_FILE)                    */
        struct {
            int16_t pipe_id;   /* id pipe anonim (TYPE_PIPE)          */
            int16_t pipe_end;  /* 0=read end, 1=write end             */
            char    _p[24];
        };
        struct {
            int16_t net_conn;  /* id koneksi TCP (TYPE_NET)           */
            char    _n[26];
        };
        struct {
            int16_t tty_id;    /* id TTY slot (TYPE_TTY)              */
            char    _t[26];
        };
    };
} VfsFd;

void vfs_init(void);               /* inisialisasi seluruh tabel (dipanggil 1x) */
void vfs_init_task(int task_id);   /* setup fd 0/1/2 untuk satu task            */
void vfs_copy_fds(int src, int dst); /* salin semua fd dari src ke dst (fork)   */
void vfs_close_all(int task_id);   /* tutup semua fd saat task exit             */

int  vfs_open (int task_id, const char *path, int flags); /* buka/buat file     */
int  vfs_read (int task_id, int fd, char *buf, int len);  /* baca dari fd       */
int  vfs_write(int task_id, int fd, const char *buf, int len); /* tulis ke fd   */
int  vfs_close(int task_id, int fd);                      /* tutup fd           */
int  vfs_seek (int task_id, int fd, int offset);          /* pindah posisi       */

/* F-R2: Pipe via VFS fd
 * Alokasi pipe dan kembalikan dua fd: fd_read dan fd_write.
 * Return 0 sukses, -1 gagal. */
int  vfs_pipe(int task_id, int *fd_read_out, int *fd_write_out);

/* F-R2: TCP socket via VFS fd
 * Buka koneksi TCP ke dst_ip:port, kembalikan fd. Return fd atau -1. */
int  vfs_net_open(int task_id, const uint8_t dst_ip[4], uint16_t port);

/* F-R2: TTY — alokasi slot pseudo-terminal untuk task, return fd */
int  vfs_tty_open(int task_id);

/* Redirect stdout/stdin ke file untuk exec + redirect shell */
int  vfs_redirect_out(int task_id, const char *path);     /* redirect stdout ke file (>) */
int  vfs_redirect_in (int task_id, const char *path);     /* redirect stdin dari file (<) */
int  vfs_stdout_is_file(int task_id);                     /* 1 jika fd 1 diredirect ke file */

/* Redirect stdout ke write-end pipe (untuk shell | operator) */
int  vfs_redirect_out_pipe(int task_id, int pipe_id);

/* Redirect stdin ke read-end pipe */
int  vfs_redirect_in_pipe(int task_id, int pipe_id);

/* Redirect stdout ke file dalam mode append (>>) */
int  vfs_redirect_out_append(int task_id, const char *path);

/* F-W1: Set flags pada fd (seperti fcntl F_SETFL) */
int  vfs_set_flags(int task_id, int fd, uint8_t flags);

/* F-W2: Cek apakah fd siap untuk operasi (untuk poll()); bitmask POLLIN/POLLOUT */
int  vfs_fd_ready(int task_id, int fd, short events);

#endif /* VFS_H */
