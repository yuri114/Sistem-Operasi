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
} Task;

void task_init();
int  task_create(void (*entry)());
int  task_create_user(uint64_t entry, uint64_t *page_dir, uint64_t user_rsp, const char *name);
void task_switch();
void task_set_main();
void task_exit();
void task_sleep(uint32_t ms);
void task_block();
void task_unblock(int id);
void task_check_sleepers();
void task_yield();

int         task_get_max();
int         task_is_used(int id);
const char *task_get_name(int id);
int         task_get_current();
int         task_kill(int id);
int         task_get_priority(int id);
int         task_set_priority(int id, int prio);
void        task_set_pipe(int id, int pipe_id);
int         task_get_current_pipe();
uint64_t    task_get_rsp0(int id);
int         task_get_status(int id);
#endif