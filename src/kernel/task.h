#ifndef TASK_H
#define TASK_H
#include <stdint.h>

#define MAX_TASKS  8
#define STACK_SIZE 4096

typedef struct{
    uint32_t esp;       // stack pointer
    uint8_t  used;      // slot aktif?
    uint32_t *page_dir; // page directory proses ini
    char     name[32];  // nama program (untuk ps)
} Task;

void task_init();
int  task_create(void (*entry)());
int  task_create_user(uint32_t entry, uint32_t *page_dir, uint32_t user_esp, const char *name);
void task_switch();
void task_set_main();
void task_exit();

// fungsi untuk ps command
int         task_get_max();          // kembalikan MAX_TASKS
int         task_is_used(int id);    // apakah slot id aktif?
const char *task_get_name(int id);   // nama program di slot id
int         task_get_current();      // id task yang sedang berjalan
int         task_kill(int id);       // matikan task berdasarkan id; 1=sukses, 0=gagal

#endif
