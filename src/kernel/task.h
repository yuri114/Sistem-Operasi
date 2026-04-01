#ifndef TASK_H
#define TASK_H
#include <stdint.h>

#define MAX_TASKS  16
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
    int       waiter;     /* tid task yang menunggu task ini selesai (-1 = none) */
    uint64_t  heap_end;   /* akhir heap user (0x400000 awal); 0 = kernel task   */
} Task;

void task_init();
int  task_create(void (*entry)());
int  task_create_user(uint64_t entry, uint64_t *page_dir, uint64_t user_rsp, const char *name);
void task_switch();           /* BSP (CPU 0) scheduler — dipanggil dari irq0   */
void task_switch_ap(int cpu_idx); /* AP scheduler — dipanggil dari lapic_timer_isr */
void task_set_has_ap(int v);       /* dipanggil smp_init setelah AP online */
void task_set_main();
void task_exit();
void task_sleep(uint32_t ms);
void task_block();
void task_unblock(int id);
void task_check_sleepers();
void task_yield();

void        task_wait(int tid);             /* tunggu task selesai (block) */
uint64_t    task_get_heap_end(int id);
void        task_set_heap_end(int id, uint64_t end);
int         task_get_max();
int         task_get_count();     /* jumlah task aktif (bukan MAX_TASKS)  */
int         task_is_used(int id);
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

/* Pointer RSP untuk context switch AP — dibaca langsung oleh lapic_timer_isr */
extern uint64_t *ap_current_rsp;
extern uint64_t *ap_next_rsp;
#endif