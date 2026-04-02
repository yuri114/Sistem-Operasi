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
} Task;

/* F-T: konstanta sinyal standar */
#define SIGINT   2   /* Ctrl+C — default terminasi */
#define SIGKILL  9   /* tidak bisa di-catch; terminasi paksa */
#define SIGTERM  15  /* sinyal terminasi lunak */

void task_init();
int  task_create(void (*entry)());
int  task_create_user(uint64_t entry, uint64_t *page_dir, uint64_t user_rsp, const char *name);
int  task_create_thread(uint64_t entry, uint64_t arg, int parent_tid);
void task_set_name(int id, const char *name); /* ubah nama task */
void task_switch();           /* BSP (CPU 0) scheduler — dipanggil dari irq0   */
void task_switch_ap(int cpu_idx); /* AP scheduler — dipanggil dari lapic_timer_isr */
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
void        task_set_pipe(int id, int pipe_id);
int         task_get_current_pipe();
uint64_t    task_get_rsp0(int id);
int         task_get_status(int id);
uint64_t   *task_get_page_dir(int id);
void        task_set_page_dir(int id, uint64_t *dir);  /* F-Q: ganti page_dir */

/* F-Q: buat task anak untuk fork(); RIP/RSP sudah di-set sesuai resume point user */
int         task_create_fork(int parent_tid, uint64_t child_rip, uint64_t child_rsp);

/* Pointer RSP untuk context switch AP — dibaca langsung oleh lapic_timer_isr */
extern uint64_t *ap_current_rsp;
extern uint64_t *ap_next_rsp;
#endif