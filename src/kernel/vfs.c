/* vfs.c — Virtual Filesystem implementation */
#include "vfs.h"
#include "task.h"
#include "fs.h"
#include "pipe.h"
#include "net.h"
#include "keyboard.h"
#include "memory.h"
#include "vmm.h"
#include "timer.h"
#include <stdint.h>

extern void print(const char *s);

/* ------------------------------------------------------------------ */
/* Fondasi AH — /proc virtual filesystem generator                     */
/* ------------------------------------------------------------------ */
/* Static buffer for generated /proc content (shared — only one proc fd at a time) */
#define PROC_BUF_SIZE 2048
static char proc_buf[PROC_BUF_SIZE];
static int  proc_buf_len = 0;

/* Simple append helpers for proc content generation */
static char *proc_wp;
static int   proc_rem;

static void proc_puts(const char *s) {
    while (*s && proc_rem > 0) { *proc_wp++ = *s++; proc_rem--; }
}
static void proc_putn(uint32_t n) {
    char buf[12]; int i = 0, j;
    if (n == 0) { proc_puts("0"); return; }
    while (n) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    /* buf is in reverse order — print forwards */
    for (j = i - 1; j >= 0; j--) { if (proc_rem > 0) { *proc_wp++ = buf[j]; proc_rem--; } }
}

static void proc_generate(const char *path) {
    proc_wp  = proc_buf;
    proc_rem = PROC_BUF_SIZE - 1;
    proc_buf_len = 0;

    /* /proc/ps */
    if (path[0]=='p'&&path[1]=='s'&&path[2]=='\0') {
        int i;
        proc_puts("TID  STATUS    NAME\n");
        proc_puts("---- --------- --------------------------------\n");
        for (i = 0; i < task_get_max(); i++) {
            if (!task_is_used(i)) continue;
            /* TID (right-padded to 4) */
            proc_putn((uint32_t)i);
            if (i < 10) proc_puts("   ");
            else if (i < 100) proc_puts("  ");
            else proc_puts(" ");
            proc_puts(" ");
            /* STATUS */
            if (i == task_get_current()) proc_puts("running   ");
            else proc_puts("blocked   ");
            /* NAME */
            const char *nm = task_get_name(i);
            if (nm) proc_puts(nm);
            proc_puts("\n");
        }
    }
    /* /proc/meminfo */
    else if (path[0]=='m'&&path[1]=='e'&&path[2]=='m') {
        uint32_t free_frames  = pmm_free_count();
        uint32_t total_frames = 16384 - 768;  /* frames 768..16383 = 15616 @ 4KB */
        uint32_t used_frames  = total_frames > free_frames ? total_frames - free_frames : 0;
        proc_puts("MemTotal:  "); proc_putn(total_frames * 4); proc_puts(" kB\n");
        proc_puts("MemFree:   "); proc_putn(free_frames  * 4); proc_puts(" kB\n");
        proc_puts("MemUsed:   "); proc_putn(used_frames  * 4); proc_puts(" kB\n");
        proc_puts("PageSize:  4 kB\n");
    }
    /* /proc/uptime */
    else if (path[0]=='u') {
        uint32_t secs = get_ticks() / 100;
        proc_putn(secs); proc_puts(" seconds\n");
    }
    /* /proc/cpuinfo */
    else if (path[0]=='c'&&path[1]=='p') {
        extern int cpu_count;
        extern volatile uint32_t smp_ap_started;
        proc_puts("arch:    x86_64\n");
        proc_puts("vendor:  Oria OS\n");
        proc_puts("mode:    Long Mode (64-bit)\n");
        proc_puts("cores:   "); proc_putn((uint32_t)cpu_count); proc_puts("\n");
        proc_puts("online:  "); proc_putn((uint32_t)(smp_ap_started + 1)); proc_puts("\n");
    }
    /* /proc/dmesg */
    else if (path[0]=='d'&&path[1]=='m') {
        extern int dmesg_read(char *buf, int maxlen);
        char dmbuf[PROC_BUF_SIZE - 1];
        int n = dmesg_read(dmbuf, PROC_BUF_SIZE - 1);
        int k;
        for (k = 0; k < n && proc_rem > 0; k++) { *proc_wp++ = dmbuf[k]; proc_rem--; }
    }
    /* /proc (directory listing) */
    else if (path[0]=='\0' || (path[0]=='.'&&path[1]=='\0')) {
        proc_puts("ps\nmeminfo\nuptime\ncpuinfo\ndmesg\n");
        /* list numeric PIDs */
        int i;
        for (i = 0; i < task_get_max(); i++) {
            if (!task_is_used(i)) continue;
            proc_putn((uint32_t)i); proc_puts("\n");
        }
    }
    /* /proc/<pid>/status  or  /proc/<pid> */
    else if (path[0] >= '0' && path[0] <= '9') {
        /* parse PID */
        int pid = 0;
        const char *p = path;
        while (*p >= '0' && *p <= '9') { pid = pid * 10 + (*p - '0'); p++; }
        /* skip optional '/status' */
        if (*p == '/') p++;
        /* accept "status" or empty suffix */
        if (!task_is_used(pid)) {
            proc_puts("proc: PID tidak ada: ");
            proc_putn((uint32_t)pid);
            proc_puts("\n");
        } else {
            const char *st_name;
            int st = task_get_status(pid);
            if (pid == task_get_current()) st_name = "R (running)";
            else if (st == TASK_SLEEPING)  st_name = "S (sleeping)";
            else if (st == TASK_BLOCKED)   st_name = "D (blocked)";
            else                            st_name = "R (ready)";
            proc_puts("Name:\t");
            const char *nm = task_get_name(pid);
            if (nm) proc_puts(nm);
            proc_puts("\n");
            proc_puts("Pid:\t"); proc_putn((uint32_t)pid); proc_puts("\n");
            proc_puts("State:\t"); proc_puts(st_name); proc_puts("\n");
            proc_puts("PPid:\t"); proc_putn((uint32_t)task_get_parent(pid)); proc_puts("\n");
            proc_puts("Priority:\t"); proc_putn((uint32_t)task_get_priority(pid)); proc_puts("\n");
        }
    }
    else {
        proc_puts("proc: unknown entry /proc/");
        proc_puts(path);
        proc_puts("\n");
    }
    proc_buf_len = (int)(proc_wp - proc_buf);
    proc_buf[proc_buf_len] = '\0';
}

/* ------------------------------------------------------------------ */

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

void vfs_copy_fds(int src, int dst)
{
    int j;
    if (src < 0 || src >= MAX_TASKS) return;
    if (dst < 0 || dst >= MAX_TASKS) return;
    for (j = 0; j < VFS_MAX_FD; j++)
        fd_table[dst][j] = fd_table[src][j];
}

void vfs_close_all(int task_id)
{
    int j;
    if (task_id < 0 || task_id >= MAX_TASKS) return;
    for (j = 0; j < VFS_MAX_FD; j++) {
        if (fd_table[task_id][j].used) {
            /* Bebaskan sumber daya yang dimiliki fd */
            if (fd_table[task_id][j].type == VFS_TYPE_NET)
                net_tcp_close((int)fd_table[task_id][j].net_conn);
            /* Jika write-end pipe ditutup, turunkan refcount → kirim EOF ke reader */
            if (fd_table[task_id][j].type == VFS_TYPE_PIPE &&
                fd_table[task_id][j].pipe_end == 1)
                pipe_writer_detach((int)fd_table[task_id][j].pipe_id);
            fd_table[task_id][j].used = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
int vfs_open(int task_id, const char *path, int flags)
{
    int j, k;
    if (task_id < 0 || task_id >= MAX_TASKS || !path) return -1;

    /* Fondasi AH: /proc virtual filesystem */
    if (path[0]=='/' && path[1]=='p' && path[2]=='r' && path[3]=='o' &&
        path[4]=='c' && path[5]=='/') {
        const char *entry = path + 6;  /* "ps", "meminfo", "uptime", "cpuinfo" */
        for (j = 3; j < VFS_MAX_FD; j++) {
            if (!fd_table[task_id][j].used) {
                fd_table[task_id][j].used   = 1;
                fd_table[task_id][j].type   = VFS_TYPE_PROC;
                fd_table[task_id][j].flags  = VFS_O_RDONLY;
                fd_table[task_id][j].offset = 0;
                for (k = 0; k < 27 && entry[k]; k++) fd_table[task_id][j].name[k] = entry[k];
                fd_table[task_id][j].name[k] = '\0';
                return j;
            }
        }
        return -1;
    }

    for (j = 3; j < VFS_MAX_FD; j++) {
        if (!fd_table[task_id][j].used) {
            /* Jika CREATE: buat file kosong jika belum ada */
            if (flags & VFS_O_CREATE) {
                if (!fs_read(path)) fs_write(path, "");
            } else {
                /* fs_read_bin agar binary file pun bisa dicek */
                uint32_t sz = 0;
                if (!fs_read_bin(path, &sz)) return -1;
            }
            fd_table[task_id][j].used   = 1;
            fd_table[task_id][j].type   = VFS_TYPE_FILE;
            fd_table[task_id][j].flags  = (uint8_t)flags;
            fd_table[task_id][j].offset = 0;
            for (k = 0; k < 27 && path[k]; k++) fd_table[task_id][j].name[k] = path[k];
            fd_table[task_id][j].name[k] = '\0';
            return j;
        }
    }
    return -1;  /* tidak ada slot kosong */
}

/* F-R2: Buka pipe — kembalikan dua fd [read_fd, write_fd].
 * Return 0 sukses, -1 gagal. */
int vfs_pipe(int task_id, int *fd_read_out, int *fd_write_out)
{
    int j, fd_r = -1, fd_w = -1, pid;
    if (task_id < 0 || task_id >= MAX_TASKS) return -1;

    pid = pipe_alloc();
    if (pid < 0) return -1;

    /* Cari 2 slot kosong */
    for (j = 3; j < VFS_MAX_FD && (fd_r < 0 || fd_w < 0); j++) {
        if (!fd_table[task_id][j].used) {
            if (fd_r < 0) fd_r = j;
            else           fd_w = j;
        }
    }
    if (fd_r < 0 || fd_w < 0) { pipe_free(pid); return -1; }

    /* Read end */
    fd_table[task_id][fd_r].used     = 1;
    fd_table[task_id][fd_r].type     = VFS_TYPE_PIPE;
    fd_table[task_id][fd_r].flags    = VFS_O_RDONLY;
    fd_table[task_id][fd_r].pipe_id  = (int16_t)pid;
    fd_table[task_id][fd_r].pipe_end = 0;  /* read */

    /* Write end */
    fd_table[task_id][fd_w].used     = 1;
    fd_table[task_id][fd_w].type     = VFS_TYPE_PIPE;
    fd_table[task_id][fd_w].flags    = VFS_O_WRONLY;
    fd_table[task_id][fd_w].pipe_id  = (int16_t)pid;
    fd_table[task_id][fd_w].pipe_end = 1;  /* write */
    pipe_writer_attach(pid);               /* tracking write-end untuk propagasi EOF */

    *fd_read_out  = fd_r;
    *fd_write_out = fd_w;
    return 0;
}

/* F-R2: Buka koneksi TCP dan bungkus ke fd.
 * Return fd sukses, -1 gagal. */
int vfs_net_open(int task_id, const uint8_t dst_ip[4], uint16_t port)
{
    int j, conn;
    if (task_id < 0 || task_id >= MAX_TASKS) return -1;
    if (!net_present()) return -1;

    conn = net_tcp_connect(dst_ip, port);
    if (conn < 0) return -1;

    for (j = 3; j < VFS_MAX_FD; j++) {
        if (!fd_table[task_id][j].used) {
            fd_table[task_id][j].used     = 1;
            fd_table[task_id][j].type     = VFS_TYPE_NET;
            fd_table[task_id][j].flags    = VFS_O_RDWR;
            fd_table[task_id][j].net_conn = (int16_t)conn;
            return j;
        }
    }
    net_tcp_close(conn);
    return -1;
}

/* TTY slot: setiap TTY punya buffer output kecil dan pipe ke app */
#define VFS_TTY_MAX  4
#define VFS_TTY_BUF  512
typedef struct {
    int  used;
    int  pipe_id;     /* pipe antara writer dan reader */
} TtySlot;
static TtySlot tty_slots[VFS_TTY_MAX];

/* F-R2: Alokasi TTY baru untuk task, return fd.
 * TTY adalah thin wrapper: write → pipe, read ← pipe. */
int vfs_tty_open(int task_id)
{
    int j, i;
    int slot = -1;
    int pid;
    if (task_id < 0 || task_id >= MAX_TASKS) return -1;

    for (i = 0; i < VFS_TTY_MAX; i++) {
        if (!tty_slots[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;

    pid = pipe_alloc();
    if (pid < 0) return -1;

    tty_slots[slot].used    = 1;
    tty_slots[slot].pipe_id = pid;

    for (j = 3; j < VFS_MAX_FD; j++) {
        if (!fd_table[task_id][j].used) {
            fd_table[task_id][j].used    = 1;
            fd_table[task_id][j].type    = VFS_TYPE_TTY;
            fd_table[task_id][j].flags   = VFS_O_RDWR;
            fd_table[task_id][j].tty_id  = (int16_t)slot;
            return j;
        }
    }
    pipe_free(pid);
    tty_slots[slot].used = 0;
    return -1;
}

/* ------------------------------------------------------------------ */
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
        /* F-W1: non-blocking — return -EAGAIN jika tidak ada data */
        if ((f->flags & VFS_O_NONBLOCK) && !keyboard_has_char())
            return VFS_EAGAIN;
        /* True blocking via keyboard_set_waiter */
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

    /* Fondasi AH: /proc virtual filesystem read */
    if (f->type == VFS_TYPE_PROC) {
        if (f->offset == 0) {
            /* (Re-)generate proc content when reading from start */
            proc_generate(f->name);
        }
        remaining = proc_buf_len - (int)f->offset;
        if (remaining <= 0) return 0;
        if (len > remaining) len = remaining;
        for (k = 0; k < len; k++) buf[k] = proc_buf[f->offset + k];
        f->offset += (uint32_t)len;
        return len;
    }

    /* F-R2: pipe read (blocking) */
    if (f->type == VFS_TYPE_PIPE) {
        if (f->pipe_end != 0) return -1;  /* bukan read end */
        if (len <= 0) return 0;
        /* F-W1: non-blocking — return -EAGAIN jika pipe kosong */
        if ((f->flags & VFS_O_NONBLOCK) && !pipe_has_data((int)f->pipe_id))
            return VFS_EAGAIN;
        /* pipe_read baca satu pesan (sampai '\0') */
        int n = pipe_read((int)f->pipe_id, buf);
        return n < len ? n : len;
    }

    /* F-R2: TCP socket read */
    if (f->type == VFS_TYPE_NET) {
        if (!net_present()) return -1;
        /* F-W1: non-blocking TCP — skip wait jika tidak ada data */
        return net_tcp_recv((int)f->net_conn, buf, (uint16_t)(len > 1400 ? 1400 : len));
    }

    /* F-R2: TTY read (dari pipe yang diisi sisi lain) */
    if (f->type == VFS_TYPE_TTY) {
        int pid = tty_slots[(int)f->tty_id].pipe_id;
        /* F-W1: non-blocking TTY */
        if ((f->flags & VFS_O_NONBLOCK) && !pipe_has_data(pid))
            return VFS_EAGAIN;
        return pipe_read(pid, buf);
    }

    return -1;
}

/* F-W1: Set fd flags (seperti fcntl F_SETFL) */
int vfs_set_flags(int task_id, int fd, uint8_t flags)
{
    if (task_id < 0 || task_id >= MAX_TASKS) return -1;
    if (fd < 0 || fd >= VFS_MAX_FD)           return -1;
    VfsFd *f = &fd_table[task_id][fd];
    if (!f->used) return -1;
    f->flags = flags;
    return 0;
}

/* F-W2: Cek apakah fd siap untuk operasi I/O (untuk poll())
 * events: bitmask POLLIN/POLLOUT
 * Return bitmask kejadian yang siap (revents), atau 0 jika tidak siap. */
int vfs_fd_ready(int task_id, int fd, short events)
{
    if (task_id < 0 || task_id >= MAX_TASKS) return 0;
    if (fd < 0 || fd >= VFS_MAX_FD)           return 0;
    VfsFd *f = &fd_table[task_id][fd];
    if (!f->used) return POLLERR;

    int ready = 0;
    if (events & POLLIN) {
        if (f->type == VFS_TYPE_STDIN && keyboard_has_char())
            ready |= POLLIN;
        else if (f->type == VFS_TYPE_PIPE && f->pipe_end == 0 &&
                 pipe_has_data((int)f->pipe_id))
            ready |= POLLIN;
        else if (f->type == VFS_TYPE_FILE)
            ready |= POLLIN;  /* file selalu siap */
        else if (f->type == VFS_TYPE_TTY &&
                 pipe_has_data(tty_slots[(int)f->tty_id].pipe_id))
            ready |= POLLIN;
        /* TYPE_NET: selalu coba — net_tcp_recv non-blocking check tidak ada */
    }
    if (events & POLLOUT) {
        /* PIPE write-end, NET, FILE, STDOUT selalu siap tulis */
        if (f->type == VFS_TYPE_PIPE && f->pipe_end == 1)
            ready |= POLLOUT;
        else if (f->type == VFS_TYPE_STDOUT || f->type == VFS_TYPE_FILE ||
                 f->type == VFS_TYPE_NET)
            ready |= POLLOUT;
    }
    return ready;
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
        int n = len < 255 ? len : 255;
        for (k = 0; k < n; k++) tmp2[k] = buf[k];
        tmp2[k] = '\0';
        print(tmp2);
        return n;
    }

    if (f->type == VFS_TYPE_FILE) {
        if (!(f->flags & (VFS_O_WRONLY | VFS_O_RDWR))) return -1;
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

    /* F-R2: pipe write — tulis sebagai null-terminated string */
    if (f->type == VFS_TYPE_PIPE) {
        if (f->pipe_end != 1) return -1;  /* bukan write end */
        /* pipe_write hanya terima null-terminated; tulis per-chunk 255 byte */
        char chunk[256];
        int written = 0;
        while (written < len) {
            int n = len - written;
            if (n > 255) n = 255;
            for (k = 0; k < n; k++) chunk[k] = buf[written + k];
            chunk[n] = '\0';
            int r = pipe_write((int)f->pipe_id, chunk);
            if (r < 0) break;
            written += r;
        }
        return written;
    }

    /* F-R2: TCP socket write */
    if (f->type == VFS_TYPE_NET) {
        if (!net_present()) return -1;
        return net_tcp_send((int)f->net_conn, buf,
                            (uint16_t)(len > 1400 ? 1400 : len));
    }

    /* F-R2: TTY write → tulis ke pipe agar reader (gui_term dll) bisa baca */
    if (f->type == VFS_TYPE_TTY) {
        char chunk[256];
        int written = 0;
        int pid = tty_slots[(int)f->tty_id].pipe_id;
        while (written < len) {
            int n = len - written;
            if (n > 255) n = 255;
            for (k = 0; k < n; k++) chunk[k] = buf[written + k];
            chunk[n] = '\0';
            int r = pipe_write(pid, chunk);
            if (r < 0) break;
            written += r;
        }
        return written;
    }

    return -1;
}

int vfs_close(int task_id, int fd)
{
    if (task_id < 0 || task_id >= MAX_TASKS) return -1;
    if (fd < 3 || fd >= VFS_MAX_FD) return -1;
    if (!fd_table[task_id][fd].used) return -1;
    VfsFd *f = &fd_table[task_id][fd];
    /* Tutup sumber daya */
    if (f->type == VFS_TYPE_NET)
        net_tcp_close((int)f->net_conn);
    if (f->type == VFS_TYPE_TTY) {
        int slot = (int)f->tty_id;
        if (tty_slots[slot].used) {
            pipe_free(tty_slots[slot].pipe_id);
            tty_slots[slot].used = 0;
        }
    }
    /* Jika write-end pipe ditutup, turunkan refcount → kirim EOF ke reader */
    if (f->type == VFS_TYPE_PIPE && f->pipe_end == 1)
        pipe_writer_detach((int)f->pipe_id);
    f->used = 0;
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

int vfs_stdout_is_file(int task_id)
{
    uint8_t t;
    if (task_id < 0 || task_id >= MAX_TASKS) return 0;
    if (!fd_table[task_id][1].used) return 0;
    t = fd_table[task_id][1].type;
    return (t == VFS_TYPE_FILE || t == VFS_TYPE_PIPE ||
            t == VFS_TYPE_NET  || t == VFS_TYPE_TTY);
}

int vfs_redirect_out(int task_id, const char *path)
{
    int k;
    if (task_id < 0 || task_id >= MAX_TASKS || !path || !path[0]) return -1;
    if (!fs_read(path)) fs_write(path, "");
    fd_table[task_id][1].used   = 1;
    fd_table[task_id][1].type   = VFS_TYPE_FILE;
    fd_table[task_id][1].flags  = VFS_O_WRONLY;
    fd_table[task_id][1].offset = 0;
    for (k = 0; k < 27 && path[k]; k++) fd_table[task_id][1].name[k] = path[k];
    fd_table[task_id][1].name[k] = '\0';
    fd_table[task_id][2] = fd_table[task_id][1];
    return 0;
}

int vfs_redirect_in(int task_id, const char *path)
{
    int k;
    if (task_id < 0 || task_id >= MAX_TASKS || !path || !path[0]) return -1;
    if (!fs_read(path)) return -1;
    fd_table[task_id][0].used   = 1;
    fd_table[task_id][0].type   = VFS_TYPE_FILE;
    fd_table[task_id][0].flags  = VFS_O_RDONLY;
    fd_table[task_id][0].offset = 0;
    for (k = 0; k < 27 && path[k]; k++) fd_table[task_id][0].name[k] = path[k];
    fd_table[task_id][0].name[k] = '\0';
    return 0;
}

/* Redirect stdout (fd 1) ke write-end sebuah pipe yang sudah ada. */
int vfs_redirect_out_pipe(int task_id, int pipe_id)
{
    if (task_id < 0 || task_id >= MAX_TASKS) return -1;
    fd_table[task_id][1].used     = 1;
    fd_table[task_id][1].type     = VFS_TYPE_PIPE;
    fd_table[task_id][1].flags    = VFS_O_WRONLY;
    fd_table[task_id][1].pipe_id  = (int16_t)pipe_id;
    fd_table[task_id][1].pipe_end = 1;
    /* fd[2] (stderr) TIDAK dialihkan ke pipe — tetap ke layar */
    pipe_writer_attach(pipe_id);  /* tracking write-end untuk propagasi EOF */
    return 0;
}

/* Redirect stdin (fd 0) ke read-end sebuah pipe yang sudah ada. */
int vfs_redirect_in_pipe(int task_id, int pipe_id)
{
    if (task_id < 0 || task_id >= MAX_TASKS) return -1;
    fd_table[task_id][0].used     = 1;
    fd_table[task_id][0].type     = VFS_TYPE_PIPE;
    fd_table[task_id][0].flags    = VFS_O_RDONLY;
    fd_table[task_id][0].pipe_id  = (int16_t)pipe_id;
    fd_table[task_id][0].pipe_end = 0;
    return 0;
}

/* Redirect stdout (fd 1) ke file dalam mode append (>>) */
int vfs_redirect_out_append(int task_id, const char *path)
{
    int k;
    uint32_t file_len = 0;
    if (task_id < 0 || task_id >= MAX_TASKS || !path || !path[0]) return -1;
    /* Set offset ke akhir file saat ini agar tulisan menyambung */
    fs_read_bin(path, &file_len);           /* jika file belum ada, file_len=0 */
    if (file_len == 0) fs_write(path, "");  /* buat file kosong supaya write valid */
    fd_table[task_id][1].used   = 1;
    fd_table[task_id][1].type   = VFS_TYPE_FILE;
    fd_table[task_id][1].flags  = VFS_O_WRONLY;
    fd_table[task_id][1].offset = file_len;
    for (k = 0; k < 27 && path[k]; k++) fd_table[task_id][1].name[k] = path[k];
    fd_table[task_id][1].name[k] = '\0';
    fd_table[task_id][2] = fd_table[task_id][1];
    return 0;
}

