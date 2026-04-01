/* vfs.c — Virtual Filesystem implementation (Tahap J) */
#include "vfs.h"
#include "task.h"
#include "fs.h"
#include "keyboard.h"
#include "memory.h"
#include <stdint.h>

extern void print(const char *s);

/* Tabel fd global: fd_table[task_id][fd_index] */
static VfsFd fd_table[MAX_TASKS][VFS_MAX_FD];

/* ------------------------------------------------------------------ */
void vfs_init(void)
{
    int i, j;
    for (i = 0; i < MAX_TASKS; i++)
        for (j = 0; j < VFS_MAX_FD; j++)
            fd_table[i][j].used = 0;
}

void vfs_init_task(int task_id)
{
    int j;
    if (task_id < 0 || task_id >= MAX_TASKS) return;
    for (j = 0; j < VFS_MAX_FD; j++) fd_table[task_id][j].used = 0;
    /* fd 0: stdin */
    fd_table[task_id][0].used  = 1;
    fd_table[task_id][0].type  = VFS_TYPE_STDIN;
    fd_table[task_id][0].flags = VFS_O_RDONLY;
    /* fd 1: stdout */
    fd_table[task_id][1].used  = 1;
    fd_table[task_id][1].type  = VFS_TYPE_STDOUT;
    fd_table[task_id][1].flags = VFS_O_WRONLY;
    /* fd 2: stderr = stdout */
    fd_table[task_id][2].used  = 1;
    fd_table[task_id][2].type  = VFS_TYPE_STDOUT;
    fd_table[task_id][2].flags = VFS_O_WRONLY;
}

void vfs_close_all(int task_id)
{
    int j;
    if (task_id < 0 || task_id >= MAX_TASKS) return;
    for (j = 0; j < VFS_MAX_FD; j++) fd_table[task_id][j].used = 0;
}

/* ------------------------------------------------------------------ */
int vfs_open(int task_id, const char *path, int flags)
{
    int j, k;
    if (task_id < 0 || task_id >= MAX_TASKS || !path) return -1;
    for (j = 3; j < VFS_MAX_FD; j++) {
        if (!fd_table[task_id][j].used) {
            /* Jika CREATE: buat file kosong jika belum ada */
            if (flags & VFS_O_CREATE) {
                if (!fs_read(path)) fs_write(path, "");
            } else {
                if (!fs_read(path)) return -1;  /* file tidak ditemukan */
            }
            fd_table[task_id][j].used   = 1;
            fd_table[task_id][j].type   = VFS_TYPE_FILE;
            fd_table[task_id][j].flags  = (uint8_t)flags;
            fd_table[task_id][j].offset = 0;
            for (k = 0; k < 31 && path[k]; k++) fd_table[task_id][j].name[k] = path[k];
            fd_table[task_id][j].name[k] = '\0';
            return j;
        }
    }
    return -1;  /* tidak ada slot kosong */
}

int vfs_read(int task_id, int fd, char *buf, int len)
{
    VfsFd *f;
    int remaining, k;
    uint32_t file_len = 0;
    const uint8_t *data;

    if (task_id < 0 || task_id >= MAX_TASKS) return -1;
    if (fd < 0 || fd >= VFS_MAX_FD)           return -1;
    f = &fd_table[task_id][fd];
    if (!f->used) return -1;

    if (f->type == VFS_TYPE_STDIN) {
        if (len < 1) return 0;
        /* Baca satu karakter dari keyboard — true blocking via keyboard_set_waiter */
        while (!keyboard_has_char()) {
            keyboard_set_waiter(task_get_current());
            task_block();
        }
        buf[0] = keyboard_getchar();
        return 1;
    }
    if (f->type == VFS_TYPE_FILE) {
        if (!(f->flags & (VFS_O_RDONLY | VFS_O_RDWR))) return -1;
        data = fs_read_bin(f->name, &file_len);
        if (!data || file_len == 0) return 0;
        remaining = (int)file_len - (int)f->offset;
        if (remaining <= 0) return 0;
        if (len > remaining) len = remaining;
        for (k = 0; k < len; k++) buf[k] = (char)data[f->offset + k];
        f->offset += (uint32_t)len;
        return len;
    }
    return -1;
}

int vfs_write(int task_id, int fd, const char *buf, int len)
{
    VfsFd *f;
    uint32_t file_len = 0;
    const uint8_t *existing;
    uint8_t *tmp;
    int end, total, k;
    char tmp2[256];

    if (task_id < 0 || task_id >= MAX_TASKS) return -1;
    if (fd < 0 || fd >= VFS_MAX_FD)           return -1;
    f = &fd_table[task_id][fd];
    if (!f->used) return -1;

    if (f->type == VFS_TYPE_STDOUT) {
        /* Cetak ke layar: salin ke buffer null-terminated sementara */
        int n = len < 255 ? len : 255;
        for (k = 0; k < n; k++) tmp2[k] = buf[k];
        tmp2[k] = '\0';
        print(tmp2);
        return n;
    }
    if (f->type == VFS_TYPE_FILE) {
        if (!(f->flags & (VFS_O_WRONLY | VFS_O_RDWR))) return -1;
        /* Baca isi lama dari heap, tambahkan/timpa di offset, tulis kembali */
        existing = fs_read_bin(f->name, &file_len);
        tmp = (uint8_t *)malloc(65536);
        if (!tmp) return -1;
        for (k = 0; k < (int)file_len && k < 65536; k++) tmp[k] = existing[k];
        end = (int)f->offset + len;
        if (end > 65535) { len = 65535 - (int)f->offset; end = 65535; }
        for (k = 0; k < len; k++) tmp[f->offset + k] = (uint8_t)buf[k];
        total = (int)file_len > end ? (int)file_len : end;
        fs_write_bin(f->name, tmp, (uint32_t)total);
        free(tmp);
        f->offset += (uint32_t)len;
        return len;
    }
    return -1;
}

int vfs_close(int task_id, int fd)
{
    if (task_id < 0 || task_id >= MAX_TASKS) return -1;
    if (fd < 3 || fd >= VFS_MAX_FD) return -1;  /* fd 0/1/2 tidak bisa ditutup */
    if (!fd_table[task_id][fd].used) return -1;
    fd_table[task_id][fd].used = 0;
    return 0;
}

int vfs_seek(int task_id, int fd, int offset)
{
    if (task_id < 0 || task_id >= MAX_TASKS) return -1;
    if (fd < 0 || fd >= VFS_MAX_FD) return -1;
    if (!fd_table[task_id][fd].used) return -1;
    fd_table[task_id][fd].offset = (uint32_t)offset;
    return 0;
}

/* Redirect stdout (fd 1) ke file path.  Buat file jika belum ada.
 * Dipanggil setelah task_create_user, sebelum task mulai jalan.
 * Return 0 sukses, -1 gagal. */
int vfs_stdout_is_file(int task_id)
{
    if (task_id < 0 || task_id >= MAX_TASKS) return 0;
    return (fd_table[task_id][1].used && fd_table[task_id][1].type == VFS_TYPE_FILE);
}

int vfs_redirect_out(int task_id, const char *path)
{
    int k;
    if (task_id < 0 || task_id >= MAX_TASKS || !path || !path[0]) return -1;
    /* Buat file kosong jika belum ada */
    if (!fs_read(path)) fs_write(path, "");
    fd_table[task_id][1].used   = 1;
    fd_table[task_id][1].type   = VFS_TYPE_FILE;
    fd_table[task_id][1].flags  = VFS_O_WRONLY;
    fd_table[task_id][1].offset = 0;
    for (k = 0; k < 31 && path[k]; k++) fd_table[task_id][1].name[k] = path[k];
    fd_table[task_id][1].name[k] = '\0';
    /* fd 2 (stderr) ikut redirect ke file yang sama */
    fd_table[task_id][2] = fd_table[task_id][1];
    return 0;
}

/* Redirect stdin (fd 0) dari file path.
 * Return 0 sukses, -1 gagal (file tidak ada). */
int vfs_redirect_in(int task_id, const char *path)
{
    int k;
    if (task_id < 0 || task_id >= MAX_TASKS || !path || !path[0]) return -1;
    if (!fs_read(path)) return -1;   /* file harus sudah ada */
    fd_table[task_id][0].used   = 1;
    fd_table[task_id][0].type   = VFS_TYPE_FILE;
    fd_table[task_id][0].flags  = VFS_O_RDONLY;
    fd_table[task_id][0].offset = 0;
    for (k = 0; k < 31 && path[k]; k++) fd_table[task_id][0].name[k] = path[k];
    fd_table[task_id][0].name[k] = '\0';
    return 0;
}
