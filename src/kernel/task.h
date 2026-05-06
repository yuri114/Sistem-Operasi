#ifndef TASK_H
#define TASK_H
#include <stdint.h>

#define MAX_TASKS  32
#define STACK_SIZE 8192

/* Status task */
#define TASK_RUNNING  0
#define TASK_BLOCKED  1
#define TASK_SLEEPING 2

typedef struct {
    uint64_t  rsp;        /* stack pointer (64-bit) */
    uint8_t   used;
    uint8_t   status;
    uint32_t  wake_tick;
    uint64_t *page_dir;   /* PML4 proses ini (uint64_t* untuk 4-level paging) */
    char      name[32];
    uint8_t   priority;
    uint8_t   ticks;
    int       pipe_id;
    int8_t    cpu_id;     /* CPU yang sedang menjalankan task ini (-1 = bebas) */
    uint8_t   is_user;    /* 1 = ring-3 user task (hanya boleh di BSP/CPU 0)   */
    uint8_t   is_thread;  /* 1 = thread (berbagi page_dir dgn parent, jangan free saat exit) */
    int       parent_tid; /* untuk thread: tid proses induk; -1 = bukan thread  */
    int       waiter;     /* tid task yang menunggu task ini selesai (-1 = none) */
    uint64_t  heap_end;   /* akhir heap user (0x400000 awal); 0 = kernel task   */
    uint64_t  tstack_frames[4]; /* frame fisik tiap halaman stack thread (0 = kosong) */
    uint32_t  pending_signals;  /* F-T: bitmask sinyal pending (bit-N = sinyal N) */
    int       exit_code;        /* F-T: kode exit; diset oleh task_exit_code()   */
    uint64_t  tls_frame;        /* F-U: frame fisik halaman TLS (0 = belum dialokasi) */
    /* Fondasi AA — argv: serialized null-terminated strings "arg0\0arg1\0..." */
    int       arg_argc;
    char      arg_buf[512];
    /* Fondasi AW — nice value (-20..+19, default 0) dan aging counter */
    int8_t    nice;       /* -20 = prioritas tertinggi, +19 = terendah */
    uint32_t  age_ticks;  /* berapa kali dilewati, direset saat dijadwalkan */
    /* Fondasi AY — uid pengguna yang menjalankan task ini (0=root) */
    int       uid;
    /* x87/SSE state: 512 byte, harus 16-byte aligned (FXSAVE/FXRSTOR) */
    uint8_t   fpu_state[512] __attribute__((aligned(16)));
    uint8_t   fpu_valid;      /* 1 jika fpu_state sudah diisi (pernah di-switch) */
    /* Ekstensi B — per-task interval timer */
    uint32_t  itimer_interval;   /* interval dalam ms (0 = timer dinonaktifkan) */
    uint32_t  itimer_remaining;  /* sisa ms hingga SIGALRM berikutnya */
    /* Tier-1: POSIX signal mask — bitmask sinyal yang sedang diblokir (SIGKILL tetap dikirim) */
    uint32_t  signal_mask;
    /* Tier-1: resource limits */
    uint32_t  rlimit_mem;  /* batas memori heap dalam KB (0 = tidak terbatas) */
    uint16_t  rlimit_fds;  /* batas jumlah file descriptor (0 = tidak terbatas) */
} Task;

/* F-T: konstanta sinyal standar */
#define SIGINT   2   /* Ctrl+C — default terminasi */
#define SIGKILL  9   /* tidak bisa di-catch; terminasi paksa */
#define SIGTERM  15  /* sinyal terminasi lunak */
#define SIGALRM  14  /* interval timer expired */

void task_init();
int  task_create(void (*entry)());
int  task_create_user(uint64_t entry, uint64_t *page_dir, uint64_t user_rsp, const char *name,
                      int argc, const char *const argv[]);
int  task_create_thread(uint64_t entry, uint64_t arg, int parent_tid);
void task_set_name(int id, const char *name); /* ubah nama task */
void task_switch();           /* BSP (CPU 0) scheduler — dipanggil dari irq0   */
void task_switch_ap(int cpu_idx); /* AP scheduler — dipanggil dari lapic_timer_isr */
void task_itimer_tick(void);  /* dipanggil dari timer_handler setiap ms — SIGALRM delivery */
void task_set_itimer(int tid, uint32_t interval_ms); /* set/clear interval timer */
void task_set_has_ap(int v);       /* dipanggil smp_init setelah AP online */
void task_set_main();
void task_exit();
void task_exit_code(int code); /* F-T: exit dengan kode — dipanggil SYS_EXIT */
void task_sleep(uint32_t ms);
void task_block();
void task_unblock(int id);
void task_check_sleepers();
void task_yield();

void        task_wait(int tid);             /* tunggu task selesai (block) */
/* F-T: sinyal + exit code */
void        task_send_signal(int tid, int sig); /* kirim sinyal ke task */
void        task_check_signals(void);       /* deliver sinyal pending current task */
int         task_get_exit_code(int tid);    /* ambil exit code setelah task selesai */
uint64_t    task_get_heap_end(int id);
void        task_set_heap_end(int id, uint64_t end);
int         task_get_max();
int         task_get_count();     /* jumlah task aktif (bukan MAX_TASKS)  */
int         task_is_used(int id);
int         task_is_thread(int id);   /* 1 jika task adalah thread */
int         task_get_parent(int id);  /* parent_tid thread, atau -1 */
const char *task_get_name(int id);
int         task_get_current();
int         task_get_cpu(int id); /* kembalikan cpu_id task */
int         task_kill(int id);
int         task_get_priority(int id);
int         task_set_priority(int id, int prio);
int         task_get_nice(int id);
int         task_set_nice(int id, int nice_val);
int         task_get_uid(int id);
void        task_set_uid(int id, int uid);
void        task_set_pipe(int id, int pipe_id);
int         task_get_current_pipe();
uint64_t    task_get_rsp0(int id);
int         task_get_status(int id);
uint64_t   *task_get_page_dir(int id);
/* Fondasi AA — argv */
int         tasks_getargc(int id);
const char *tasks_getargbuf(int id);
void        task_set_page_dir(int id, uint64_t *dir);  /* F-Q: ganti page_dir */
/* Tier-1: signal mask */
void     task_set_signal_mask(int tid, uint32_t mask);
uint32_t task_get_signal_mask(int tid);
/* Tier-1: resource limits */
void     task_set_rlimit(int tid, uint32_t mem_kb, uint16_t fds);
void     task_get_rlimit(int tid, uint32_t *mem_kb, uint16_t *fds);

/* F-Q: buat task anak untuk fork(); RIP/RSP sudah di-set sesuai resume point user */
int         task_create_fork(int parent_tid, uint64_t child_rip, uint64_t child_rsp);

/* Pointer RSP untuk context switch AP — dibaca langsung oleh lapic_timer_isr */
extern uint64_t *ap_current_rsp;
extern uint64_t *ap_next_rsp;
#endif