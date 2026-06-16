#include "syscall.h"
#include "syscall_internal.h"
#include "ipc.h"
#include "task.h"
#include "semaphore.h"
#include "pipe.h"
#include "shm.h"
#include "mq.h"
#include "condvar.h"

/* -----------------------------------------------------------------------
 * F-U1: Futex table — maks 32 waiter sekaligus
 * ----------------------------------------------------------------------- */
typedef struct {
    uint64_t addr;  /* alamat virtual user yang ditunggu */
    int      tid;   /* task yang menunggu */
    int      used;  /* 1 = slot terpakai */
} FutexEntry;

#define FUTEX_TABLE_SIZE 32
static FutexEntry futex_table[FUTEX_TABLE_SIZE];

uint64_t syscall_dispatch_ipc(uint64_t eax, uint64_t ebx, uint64_t edx, int *handled) {
    *handled = 1;

    // SYS_MSG_SEND(7): ebx = pointer string pesan
    if (eax == SYS_MSG_SEND) {
        if (!is_user_ptr(ebx)) return 0;
        return (uint32_t)ipc_send((const char*)ebx);
    }
    // SYS_MSG_RECV(8): ebx = pointer buffer tujuan (minimal 64 byte)
    if (eax == SYS_MSG_RECV) {
        if (!is_user_ptr(ebx)) return 0;
        return (uint32_t)ipc_recv((char*)ebx);
    }

    // SYS_SEM_ALLOC(10): ebx = nilai awal (biasanya 1)
    if (eax == SYS_SEM_ALLOC) {
        return (uint32_t)sem_alloc((int)ebx);
    }
    // SYS_SEM_FREE(11): ebx = id semaphore
    if (eax == SYS_SEM_FREE) {
        sem_free((int)ebx);
        return 0;
    }
    // SYS_SEM_WAIT(12): ebx = id semaphore — block sampai bebas
    if (eax == SYS_SEM_WAIT) {
        return (uint32_t)sem_wait((int)ebx);
    }
    // SYS_SEM_POST(13): ebx = id semaphore — release
    if (eax == SYS_SEM_POST) {
        return (uint32_t)sem_post((int)ebx);
    }

    // SYS_PIPE_OPEN(14): alokasi pipe baru — return id (0-7) atau -1
    if (eax == SYS_PIPE_OPEN) {
        return (uint32_t)pipe_alloc();
    }
    // SYS_PIPE_WRITE(15): ebx=id, edx=pointer string — tulis ke pipe
    if (eax == SYS_PIPE_WRITE) {
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        return (uint32_t)pipe_write((int)ebx, (const char*)edx);
    }
    // SYS_PIPE_READ(16): ebx=id, edx=pointer buffer — baca satu pesan dari pipe
    if (eax == SYS_PIPE_READ) {
        if (!is_user_ptr(edx)) return (uint32_t)-1;
        return (uint32_t)pipe_read((int)ebx, (char*)edx);
    }
    // SYS_PIPE_CLOSE(17): ebx=id — bebaskan slot pipe
    if (eax == SYS_PIPE_CLOSE) {
        pipe_free((int)ebx);
        return 0;
    }
    // SYS_PIPE_GETID(18): kembalikan pipe_id yang diwarisi task saat ini dari shell
    if (eax == SYS_PIPE_GETID) {
        return (uint32_t)task_get_current_pipe();
    }

    // SYS_PIPE_NAMED(52): buka/buat named pipe: ebx=nama
    if (eax == SYS_PIPE_NAMED) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        return (uint64_t)named_pipe_open((const char*)ebx);
    }

    // SYS_SHM_CREATE(53): buat shared memory segment: ebx=key
    if (eax == SYS_SHM_CREATE) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        return (uint64_t)shm_create((const char*)ebx);
    }

    // SYS_SHM_ATTACH(54): map shm segment ke proses pemanggil: ebx=shm_id
    if (eax == SYS_SHM_ATTACH) {
        int tid = task_get_current();
        uint64_t *pml4 = task_get_page_dir(tid);
        return (uint64_t)shm_attach((int)ebx, pml4);
    }

    // SYS_SHM_DETACH(55): lepas shm dari proses: ebx=shm_id
    if (eax == SYS_SHM_DETACH) {
        int tid = task_get_current();
        uint64_t *pml4 = task_get_page_dir(tid);
        shm_detach((int)ebx, pml4);
        return 0;
    }

    /* ---- Tahap L: Message Queue syscalls ---- */
    // SYS_MQ_SEND(60): kirim pesan; ebx=dst_pid, edx=str_ptr (max 56 char)
    if (eax == SYS_MQ_SEND) {
        if (!is_user_ptr(edx)) return (uint64_t)-1;
        const char *msg = (const char*)edx;
        int from = task_get_current();
        /* hitung panjang (bounded) */
        int len = 0;
        while (len < MQ_MSG_SIZE && msg[len]) len++;
        return (uint64_t)(mq_send((int)ebx, (const uint8_t*)msg, len, from) == 0 ? 1 : 0);
    }
    // SYS_MQ_RECV(61): terima pesan; ebx=ptr{int from;int len;char data[56];}
    if (eax == SYS_MQ_RECV) {
        if (!is_user_ptr(ebx)) return 0;
        typedef struct { int from; int len; char data[MQ_MSG_SIZE]; } MqRes;
        MqRes *r = (MqRes*)ebx;
        int from = -1;
        int n = mq_recv(task_get_current(), (uint8_t*)r->data, MQ_MSG_SIZE, &from);
        r->from = from;
        r->len  = n;
        return (uint64_t)(n > 0 ? 1 : 0);
    }

    // SYS_COND_ALLOC(68): alokasi condvar → return id
    if (eax == SYS_COND_ALLOC) {
        return (uint64_t)(int64_t)cv_alloc();
    }

    // SYS_COND_FREE(69): bebaskan condvar; ebx=id
    if (eax == SYS_COND_FREE) {
        cv_free((int)ebx);
        return 0;
    }

    // SYS_COND_WAIT(70): cv_wait; ebx=cond_id, edx=sem_id
    if (eax == SYS_COND_WAIT) {
        return (uint64_t)(int64_t)cv_wait((int)ebx, (int)edx);
    }

    // SYS_COND_SIGNAL(71): cv_signal; ebx=cond_id
    if (eax == SYS_COND_SIGNAL) {
        return (uint64_t)(int64_t)cv_signal((int)ebx);
    }

    // SYS_COND_BROADCAST(72): cv_broadcast; ebx=cond_id
    if (eax == SYS_COND_BROADCAST) {
        return (uint64_t)(int64_t)cv_broadcast((int)ebx);
    }

    // SYS_FUTEX_WAIT(89): jika *(int*)ebx == (int)edx → masuk futex_table, blok, return 0
    //                     jika nilai sudah berubah → return -1 (tidak blok)
    if (eax == SYS_FUTEX_WAIT) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        // Baca nilai atomik dari user space
        int cur = *(volatile int *)(uint64_t)ebx;
        if (cur != (int)edx) return (uint64_t)-1;  // nilai sudah lain, tidak perlu tunggu

        // Daftarkan ke futex_table
        int slot = -1, fi;
        for (fi = 0; fi < FUTEX_TABLE_SIZE; fi++) {
            if (!futex_table[fi].used) { slot = fi; break; }
        }
        if (slot == -1) return (uint64_t)-1;  // table penuh

        int cur_tid = task_get_current();
        futex_table[slot].addr = (uint64_t)ebx;
        futex_table[slot].tid  = cur_tid;
        futex_table[slot].used = 1;

        task_block();  // blok sampai SYS_FUTEX_WAKE membangunkan
        return 0;
    }

    // SYS_FUTEX_WAKE(90): bangunkan maks (int)edx waiter yang menunggu di addr ebx
    if (eax == SYS_FUTEX_WAKE) {
        int n_wake = (int)edx;
        int count  = 0, fi;
        for (fi = 0; fi < FUTEX_TABLE_SIZE && count < n_wake; fi++) {
            if (futex_table[fi].used && futex_table[fi].addr == (uint64_t)ebx) {
                int wtid = futex_table[fi].tid;
                futex_table[fi].used = 0;
                futex_table[fi].addr = 0;
                task_unblock(wtid);
                count++;
            }
        }
        return (uint64_t)(uint32_t)count;
    }

    *handled = 0;
    return 0;
}
