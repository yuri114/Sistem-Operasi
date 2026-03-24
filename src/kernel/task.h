#ifndef TASK_H
#define TASK_H
#include <stdint.h>

#define MAX_TASKS  16
#define STACK_SIZE 8192

/* Status task */
#define TASK_RUNNING  0
#define TASK_BLOCKED  1   /* blokir menunggu event (semaphore, pipe, dll) */
#define TASK_SLEEPING 2   /* tidur sampai wake_tick */

typedef struct{
    uint32_t esp;         // stack pointer
    uint8_t  used;        // slot aktif?
    uint8_t  status;      // TASK_RUNNING / TASK_BLOCKED / TASK_SLEEPING
    uint32_t wake_tick;   // untuk TASK_SLEEPING: ticks saat harus dibangunkan
    uint32_t *page_dir;   // page directory proses ini
    char     name[32];    // nama program (untuk ps)
    uint8_t  priority;    // 1=rendah, 2=normal, 3=tinggi
    uint8_t  ticks;       // sisa slot CPU siklus ini
    int      pipe_id;     // pipe yang diwarisi dari shell (-1 = tidak ada)
} Task;

void task_init();
int  task_create(void (*entry)());
int  task_create_user(uint32_t entry, uint32_t *page_dir, uint32_t user_esp, const char *name);
void task_switch();
void task_set_main();
void task_exit();
void task_sleep(uint32_t ms);    // tidurkan task saat ini selama ms milidetik
void task_block();               // blokir task saat ini (dibangunkan oleh task_unblock)
void task_unblock(int id);       // bangunkan task yang diblokir
void task_check_sleepers();      // dipanggil timer: bangunkan task yang waktunya habis
void task_yield();               // lepas sisa slot CPU agar task lain dapat giliran

// fungsi untuk ps command
int         task_get_max();
int         task_is_used(int id);
const char *task_get_name(int id);
int         task_get_current();
int         task_kill(int id);
int         task_get_priority(int id);
int         task_set_priority(int id, int prio);
void        task_set_pipe(int id, int pipe_id);
int         task_get_current_pipe();
uint32_t    task_get_esp0(int id);
int         task_get_status(int id);  // kembalikan status task
#endif
