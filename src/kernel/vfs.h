/* vfs.h — Virtual Filesystem layer (Tahap J)
 *
 * Abstraksi file descriptor per-task di atas MFS3.
 * Setiap task punya tabel hingga VFS_MAX_FD descriptor.
 * fd=0: stdin (keyboard), fd=1: stdout (print), fd=2: stderr,
 * fd=3..7: file MFS3 yang dibuka via vfs_open().
 */
#ifndef VFS_H
#define VFS_H
#include <stdint.h>

#define VFS_MAX_FD    8    /* max fd per task                         */
#define VFS_FD_STDIN  0    /* standard input  (keyboard)              */
#define VFS_FD_STDOUT 1    /* standard output (screen)                */
#define VFS_FD_STDERR 2    /* standard error  (= stdout for now)      */

/* Tipe fd */
#define VFS_TYPE_NONE   0
#define VFS_TYPE_STDIN  1
#define VFS_TYPE_STDOUT 2
#define VFS_TYPE_FILE   3  /* MFS3 file dengan offset baca/tulis      */

/* Flag open */
#define VFS_O_RDONLY  0x01
#define VFS_O_WRONLY  0x02
#define VFS_O_RDWR    0x03
#define VFS_O_CREATE  0x04  /* buat file jika belum ada               */

/* Descriptor per-fd */
typedef struct {
    uint8_t  type;        /* VFS_TYPE_*                               */
    uint8_t  used;        /* 1 = slot terpakai                        */
    uint8_t  flags;       /* VFS_O_*                                  */
    uint8_t  _pad;
    uint32_t offset;      /* posisi baca/tulis saat ini (TYPE_FILE)   */
    char     name[32];    /* nama file di MFS3 (TYPE_FILE)            */
} VfsFd;

void vfs_init(void);               /* inisialisasi seluruh tabel (dipanggil 1x) */
void vfs_init_task(int task_id);   /* setup fd 0/1/2 untuk satu task            */
void vfs_close_all(int task_id);   /* tutup semua fd saat task exit             */

int  vfs_open (int task_id, const char *path, int flags); /* buka/buat file     */
int  vfs_read (int task_id, int fd, char *buf, int len);  /* baca dari fd       */
int  vfs_write(int task_id, int fd, const char *buf, int len); /* tulis ke fd   */
int  vfs_close(int task_id, int fd);                      /* tutup fd >= 3      */
int  vfs_seek (int task_id, int fd, int offset);          /* pindah posisi       */

#endif /* VFS_H */
