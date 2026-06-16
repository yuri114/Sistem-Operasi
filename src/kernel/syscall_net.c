#include "syscall.h"
#include "syscall_internal.h"
#include "vfs.h"
#include "net.h"
#include "task.h"

uint64_t syscall_dispatch_net(uint64_t eax, uint64_t ebx, uint64_t edx, int *handled) {
    *handled = 1;

    // SYS_PIPE2(77): buat pipe, kembalikan dua fd di array int[2].
    // ebx = ptr int[2] { fd_read, fd_write }
    if (eax == SYS_PIPE2) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        int *fds = (int *)ebx;
        int fd_r = -1, fd_w = -1;
        int tid = task_get_current();
        int r = vfs_pipe(tid, &fd_r, &fd_w);
        fds[0] = fd_r;
        fds[1] = fd_w;
        return (uint64_t)(int64_t)r;
    }

    // SYS_NET_OPEN(78): buka TCP socket ke ip:port, kembalikan fd.
    // ebx = ptr struct { uint8_t ip[4]; uint16_t port; }
    if (eax == SYS_NET_OPEN) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        typedef struct { uint8_t ip[4]; uint16_t port; } NetArgs;
        NetArgs *a = (NetArgs *)ebx;
        int tid = task_get_current();
        return (uint64_t)(int64_t)vfs_net_open(tid, a->ip, a->port);
    }

    // SYS_TTY_OPEN(79): alokasi TTY fd untuk task saat ini.
    if (eax == SYS_TTY_OPEN) {
        int tid = task_get_current();
        return (uint64_t)(int64_t)vfs_tty_open(tid);
    }

    // SYS_PIPE_REDIRECT(80): redirect stdout/stdin ke pipe.
    // ebx = target_tid, edx = (dir<<24)|pipe_id
    //   dir=0 → redirect stdin  (fd 0) ke read-end pipe
    //   dir=1 → redirect stdout (fd 1) ke write-end pipe
    if (eax == SYS_PIPE_REDIRECT) {
        int target    = (int)ebx;
        int pipe_id   = (int)(edx & 0xFFFFFF);
        int dir       = (int)(edx >> 24);
        if (dir == 1)
            return (uint64_t)(int64_t)vfs_redirect_out_pipe(target, pipe_id);
        else
            return (uint64_t)(int64_t)vfs_redirect_in_pipe(target, pipe_id);
    }

    // SYS_NET_LISTEN(101): mulai listen TCP: ebx=port → listen_id / -1
    if (eax == SYS_NET_LISTEN) {
        uint16_t port = (uint16_t)(ebx & 0xFFFF);
        return (uint64_t)(int)net_tcp_listen(port);
    }

    // SYS_NET_ACCEPT(102): terima koneksi: ebx=listen_id, edx=timeout_ms → conn_id / -1
    if (eax == SYS_NET_ACCEPT) {
        int listen_id   = (int)(unsigned int)ebx;
        uint32_t tmo_ms = (uint32_t)edx;
        return (uint64_t)(int)net_tcp_accept(listen_id, tmo_ms);
    }

    // SYS_NET_UNLISTEN(103): berhenti listen: ebx=listen_id
    if (eax == SYS_NET_UNLISTEN) {
        net_tcp_unlisten((int)(unsigned int)ebx);
        return 0;
    }

    *handled = 0;
    return 0;
}
