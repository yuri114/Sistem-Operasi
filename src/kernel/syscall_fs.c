#include "syscall.h"
#include "syscall_internal.h"
#include "fs.h"
#include "device.h"
#include "vfs.h"
#include "mfs4.h"
#include "task.h"
#include "timer.h"

uint64_t syscall_dispatch_fs(uint64_t eax, uint64_t ebx, uint64_t edx, int *handled) {
    *handled = 1;

    // SYS_FS_READ(5): ebx = pointer nama file (userspace), edx = pointer buffer tujuan (userspace)
    // Kernel menyalin isi file ke buffer user. Return: jumlah byte yang disalin, atau 0 jika gagal.
    if (eax == SYS_FS_READ) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return 0;
        const char *data = fs_read((const char*)ebx);
        if (!data) return 0;
        char *ubuf = (char*)edx;
        uint32_t n = 0;
        while (data[n] && n < FS_MAX_DATA - 1u) { ubuf[n] = data[n]; n++; }
        ubuf[n] = '\0';
        return n;
    }
    // SYS_FS_WRITE(6): ebx = pointer ke struct { const char *name; const char *data; }
    // return: 1 sukses, 0 gagal
    if (eax == SYS_FS_WRITE) {
        if (!is_user_ptr(ebx)) return 0;
        const char **args = (const char**)ebx;
        if (!is_user_ptr((uint64_t)args[0]) || !is_user_ptr((uint64_t)args[1])) return 0;
        const char *name = args[0];
        const char *data = args[1];
        return (uint32_t)fs_write(name, data);
    }

    // SYS_DEV_WRITE(19): ebx=dev_id, edx=pointer string
    if (eax == SYS_DEV_WRITE) {
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        return (uint32_t)dev_write((int)ebx, (const char*)edx);
    }
    // SYS_DEV_READ(20): ebx=dev_id, edx=pointer buffer
    if (eax == SYS_DEV_READ) {
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        return (uint32_t)dev_read((int)ebx, (char*)edx);
    }
    // SYS_DEV_IOCTL(21): ebx=dev_id, edx=cmd<<16|arg
    if (eax == SYS_DEV_IOCTL) {
        int cmd = (int)((edx >> 16) & 0xFFFF);
        int arg = (int)(edx & 0xFFFF);
        return (uint32_t)dev_ioctl((int)ebx, cmd, arg);
    }

    // SYS_FS_LIST(46): list nama file ke buffer
    if (eax == SYS_FS_LIST) {
        if (!is_user_ptr(ebx)) return 0;
        if ((int)edx <= 0) return 0;
        return (uint32_t)fs_list_buf((char*)ebx, (int)edx);
    }

    // SYS_FS_DELETE(47): hapus file berdasarkan nama
    if (eax == SYS_FS_DELETE) {
        if (!is_user_ptr(ebx)) return 0;
        return (uint32_t)fs_delete((const char*)ebx);
    }

    // SYS_FS_SYNC(49): flush semua dirty file ke disk
    if (eax == SYS_FS_SYNC) {
        return (uint64_t)fs_flush();
    }

    // SYS_FS_TMPWRITE(50): tulis data ke tmpfs: ebx=nama, edx=ptr FSWriteArgs
    if (eax == SYS_FS_TMPWRITE) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return 0;
        typedef struct { const uint8_t *data; uint32_t size; } WArgs;
        WArgs *a = (WArgs*)edx;
        return (uint64_t)fs_write_tmp((const char*)ebx, a->data, a->size);
    }

    // SYS_FS_MKDIR(51): buat direktori: ebx=nama
    if (eax == SYS_FS_MKDIR) {
        if (!is_user_ptr(ebx)) return 0;
        return (uint64_t)fs_mkdir((const char*)ebx);
    }

    /* ---- Tahap J: VFS syscalls ---- */
    // SYS_OPEN(56): buka file; ebx=path_ptr, edx=flags
    if (eax == SYS_OPEN) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        int tid = task_get_current();
        return (uint64_t)vfs_open(tid, (const char*)ebx, (int)edx);
    }
    // SYS_READ_FD(57): baca fd; ebx=fd, edx=ptr{char*buf, int len}
    if (eax == SYS_READ_FD) {
        if (!is_user_ptr(edx)) return (uint64_t)-1;
        typedef struct { char *buf; int len; } RArgs;
        RArgs *a = (RArgs*)edx;
        if (!is_user_ptr((uint64_t)a->buf)) return (uint64_t)-1;
        if (a->len < 0 || a->len > 65536) return (uint64_t)-1;
        int tid = task_get_current();
        return (uint64_t)vfs_read(tid, (int)ebx, a->buf, a->len);
    }
    // SYS_WRITE_FD(58): tulis fd; ebx=fd, edx=ptr{const char*buf, int len}
    if (eax == SYS_WRITE_FD) {
        if (!is_user_ptr(edx)) return (uint64_t)-1;
        typedef struct { const char *buf; int len; } WArgs;
        WArgs *a = (WArgs*)edx;
        if (!is_user_ptr((uint64_t)a->buf)) return (uint64_t)-1;
        if (a->len < 0 || a->len > 65536) return (uint64_t)-1;
        int tid = task_get_current();
        return (uint64_t)vfs_write(tid, (int)ebx, a->buf, a->len);
    }
    // SYS_CLOSE_FD(59): tutup fd; ebx=fd
    if (eax == SYS_CLOSE_FD) {
        int tid = task_get_current();
        return (uint64_t)vfs_close(tid, (int)ebx);
    }

    // ---------------------------------------------------------------
    // F-R3 — MFS4 inode layer syscalls
    // ---------------------------------------------------------------

    // SYS_MFS4_SYMLINK(81): buat symlink; ebx=link_path_ptr, edx=target_path_ptr
    if (eax == SYS_MFS4_SYMLINK) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_symlink((const char*)ebx, (const char*)edx);
    }

    // SYS_MFS4_HARDLINK(82): buat hardlink; ebx=link_path_ptr, edx=orig_path_ptr
    if (eax == SYS_MFS4_HARDLINK) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_hardlink((const char*)ebx, (const char*)edx);
    }

    // SYS_MFS4_STAT(83): stat file; ebx=path_ptr, edx=ptr MFS4Stat
    if (eax == SYS_MFS4_STAT) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_stat((const char*)ebx, (MFS4Stat*)edx);
    }

    // SYS_MFS4_LISTDIR(84): list dir; ebx=dir_ptr, edx=ptr{char*buf, int bufsz}
    if (eax == SYS_MFS4_LISTDIR) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return (uint64_t)-1;
        typedef struct { char *buf; int bufsz; } LDArgs;
        LDArgs *a = (LDArgs*)edx;
        if (!is_user_ptr((uint64_t)a->buf)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_listdir((const char*)ebx, a->buf, a->bufsz);
    }

    // SYS_MFS4_UNLINK(85): unlink; ebx=path_ptr
    if (eax == SYS_MFS4_UNLINK) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_unlink((const char*)ebx);
    }

    // SYS_MFS4_MKDIR(86): mkdir via MFS4; ebx=path_ptr
    if (eax == SYS_MFS4_MKDIR) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_mkdir((const char*)ebx);
    }

    // SYS_MFS4_RENAME(92): rename inode MFS4; ebx=old_path, edx=new_path → 0/-1
    if (eax == SYS_MFS4_RENAME) {
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return (uint64_t)-1;
        return (uint64_t)(int64_t)mfs4_rename((const char*)ebx, (const char*)edx);
    }

    // SYS_FCNTL(93): set fd flags; ebx=fd, edx=new_flags → 0/-1
    if (eax == SYS_FCNTL) {
        int fd   = (int)(int64_t)ebx;
        uint8_t new_flags = (uint8_t)(uint64_t)edx;
        int tid = task_get_current();
        return (uint64_t)(int64_t)vfs_set_flags(tid, fd, new_flags);
    }

    // SYS_POLL(94): poll array fd dengan timeout
    // ebx = pointer ke struct { KPollFd *fds; int nfds; int timeout_ms; }
    if (eax == SYS_POLL) {
        typedef struct { uint64_t fds_ptr; int nfds; int timeout_ms; } _PollArgs;
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        _PollArgs *a = (_PollArgs*)ebx;
        if (!is_user_ptr(a->fds_ptr)) return (uint64_t)-1;
        KPollFd *fds = (KPollFd*)a->fds_ptr;
        int nfds       = a->nfds;
        int timeout_ms = a->timeout_ms;
        if (nfds <= 0 || nfds > 16) return (uint64_t)-1;
        int tid = task_get_current();
        uint32_t deadline = get_ticks() + (uint32_t)(timeout_ms > 0 ? timeout_ms : 0);

        while (1) {
            int ready = 0;
            int i;
            for (i = 0; i < nfds; i++) {
                fds[i].revents = (short)vfs_fd_ready(tid, fds[i].fd, fds[i].events);
                if (fds[i].revents) ready++;
            }
            if (ready > 0)
                return (uint64_t)(int64_t)ready;
            if (timeout_ms == 0 || get_ticks() >= deadline)
                return 0;
            task_sleep(1);  /* yield 1 ms, coba lagi */
        }
    }

    // SYS_LSEEK(100): pindah posisi fd: ebx=fd, edx=offset (int32) → new offset / -1
    if (eax == SYS_LSEEK) {
        int fd     = (int)(unsigned int)ebx;
        int offset = (int)edx;
        int tid = task_get_current();
        int r = vfs_seek(tid, fd, offset);
        return (r == 0) ? (uint64_t)(uint32_t)offset : (uint64_t)-1;
    }

    *handled = 0;
    return 0;
}
