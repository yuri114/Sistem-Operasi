/* task.c ? Penjadwal task (priority weighted round-robin) 64-bit Long Mode */
#include "task.h"
#include "timer.h"
#include "vmm.h"
#include "tss.h"
#include "memory.h"

static void str_copy_n(char *dst, const char *src, int n) {
    int i;
    for (i = 0; i < n - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static Task tasks[MAX_TASKS];
static uint8_t *stacks_base;
static int current_task = 0;
static int task_count   = 0;

/* Ditulis task_switch, dibaca irq0.
 * current_rsp: alamat tasks[current].rsp (untuk simpan RSP lama)
 * next_rsp   : alamat tasks[next].rsp    (untuk muat RSP baru) */
uint64_t *current_rsp;
uint64_t *next_rsp;

void task_init() {
    int i;
    stacks_base = (uint8_t *)malloc((uint32_t)(MAX_TASKS * STACK_SIZE));
    for (i = 0; i < MAX_TASKS; i++) {
        tasks[i].used      = 0;
        tasks[i].status    = TASK_RUNNING;
        tasks[i].wake_tick = 0;
    }
}

void task_set_main() {
    task_count = 1;
    tasks[0].used      = 1;
    tasks[0].status    = TASK_RUNNING;
    tasks[0].wake_tick = 0;
    tasks[0].priority  = 3;
    tasks[0].ticks     = 3;
    tasks[0].pipe_id   = -1;
    /* Gunakan boot PML4 di alamat fisik 0x1000 (identity mapped = VA 0x1000) */
    tasks[0].page_dir  = (uint64_t *)0x1000;
    str_copy_n(tasks[0].name, "[shell]", 32);
}

int task_create(void (*entry)()) {
    if (task_count >= MAX_TASKS) return -1;

    int id = task_count++;
    tasks[id].used      = 1;
    tasks[id].status    = TASK_RUNNING;
    tasks[id].wake_tick = 0;
    tasks[id].priority  = 1;
    tasks[id].ticks     = 1;
    tasks[id].pipe_id   = -1;
    str_copy_n(tasks[id].name, "[idle]", 32);
    tasks[id].page_dir  = vmm_create_page_dir();

    /* Susun stack awal:
     *   [tinggi] RFLAGS, CS, RIP   (iretq frame ring-0->ring-0, 3 qword)
     *   [rendah] 15 x 0            (GPR slots: rax..r15) */
    uint64_t *stack_top = (uint64_t *)(stacks_base + (uint64_t)id * STACK_SIZE + STACK_SIZE);
    *(--stack_top) = 0x202;               /* RFLAGS: IF=1 */
    *(--stack_top) = 0x08;                /* CS: kernel code */
    *(--stack_top) = (uint64_t)entry;     /* RIP */
    int k;
    for (k = 0; k < 15; k++) *(--stack_top) = 0; /* rax,rbx,rcx,rdx,rsi,rdi,rbp,r8-r15 */

    tasks[id].rsp = (uint64_t)stack_top;
    return id;
}

int task_create_user(uint64_t entry, uint64_t *page_dir, uint64_t user_rsp, const char *name) {
    int id = -1, i;
    for (i = 1; i < MAX_TASKS; i++) {
        if (!tasks[i].used) { id = i; break; }
    }
    if (id == -1) return -1;
    if (id >= task_count) task_count = id + 1;

    tasks[id].used      = 1;
    tasks[id].status    = TASK_RUNNING;
    tasks[id].wake_tick = 0;
    tasks[id].page_dir  = page_dir;
    tasks[id].priority  = 2;
    tasks[id].ticks     = 2;
    tasks[id].pipe_id   = -1;
    str_copy_n(tasks[id].name, name ? name : "?", 32);

    /* Susun stack awal:
     *   [tinggi] SS, RSP_user, RFLAGS, CS_user, RIP  (iretq frame ring-0->ring-3, 5 qword)
     *   [rendah] 15 x 0                              (GPR slots) */
    uint64_t *stack_top = (uint64_t *)(stacks_base + (uint64_t)id * STACK_SIZE + STACK_SIZE);
    *(--stack_top) = 0x23;              /* SS: user data selector */
    *(--stack_top) = user_rsp;          /* RSP: user mode stack */
    *(--stack_top) = 0x202;             /* RFLAGS: IF=1 */
    *(--stack_top) = 0x1B;              /* CS: user code selector */
    *(--stack_top) = entry;             /* RIP: entry point ELF */
    int k;
    for (k = 0; k < 15; k++) *(--stack_top) = 0;

    tasks[id].rsp = (uint64_t)stack_top;
    return id;
}

void task_switch() {
    current_rsp = 0;
    next_rsp    = 0;

    if (task_count < 2) return;

    if (tasks[current_task].used &&
        tasks[current_task].status == TASK_RUNNING &&
        tasks[current_task].ticks > 1) {
        tasks[current_task].ticks--;
        return;
    }

    if (tasks[current_task].used && tasks[current_task].status == TASK_RUNNING)
        tasks[current_task].ticks = tasks[current_task].priority;

    int next = (current_task + 1) % task_count;
    int i;
    for (i = 0; i < task_count; i++) {
        if (tasks[next].used && tasks[next].status == TASK_RUNNING) break;
        next = (next + 1) % task_count;
    }
    if (!tasks[next].used || tasks[next].status != TASK_RUNNING) return;
    if (next == current_task) return;

    current_rsp  = &tasks[current_task].rsp;
    next_rsp     = &tasks[next].rsp;
    current_task = next;

    tss64_set_kernel_stack((uint64_t)(stacks_base + (uint64_t)next * STACK_SIZE + STACK_SIZE));

    if (tasks[current_task].page_dir)
        vmm_switch_dir(tasks[current_task].page_dir);
}

void task_exit() {
    uint64_t *dir = tasks[current_task].page_dir;
    tasks[current_task].used     = 0;
    tasks[current_task].page_dir = 0;

    /* Kembali ke boot PML4 dulu sebelum membebaskan page_dir proses */
    vmm_switch_dir((uint64_t *)0x1000);
    vmm_free_user_memory(dir);

    __asm__ volatile ("sti");
    while (1) __asm__ volatile ("hlt");
}

int task_get_max()       { return MAX_TASKS; }
int task_is_used(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].used : 0; }
const char *task_get_name(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].name : ""; }
int task_get_current() { return current_task; }
int task_get_priority(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].priority : 0; }

int task_set_priority(int id, int prio) {
    if (id < 0 || id >= MAX_TASKS || !tasks[id].used) return 0;
    if (prio < 1 || prio > 3) return 0;
    tasks[id].priority = (uint8_t)prio;
    tasks[id].ticks    = (uint8_t)prio;
    return 1;
}

void task_set_pipe(int id, int pipe_id) {
    if (id >= 0 && id < MAX_TASKS) tasks[id].pipe_id = pipe_id;
}

int task_get_current_pipe() { return tasks[current_task].pipe_id; }

int task_kill(int id) {
    if (id <= 0 || id >= MAX_TASKS) return 0;
    if (!tasks[id].used) return 0;
    uint64_t *dir   = tasks[id].page_dir;
    tasks[id].used      = 0;
    tasks[id].page_dir  = 0;
    vmm_free_user_memory(dir);
    if (id == task_count - 1) task_count--;
    return 1;
}

uint64_t task_get_rsp0(int id) {
    if (id < 0 || id >= MAX_TASKS) return 0;
    return (uint64_t)(stacks_base + (uint64_t)id * STACK_SIZE + STACK_SIZE);
}

void task_sleep(uint32_t ms) {
    uint32_t wait_ticks = (ms * 100 + 999) / 1000;
    if (wait_ticks == 0) wait_ticks = 1;
    tasks[current_task].status    = TASK_SLEEPING;
    tasks[current_task].wake_tick = get_ticks() + wait_ticks;
    __asm__ volatile ("sti");
    while (tasks[current_task].status == TASK_SLEEPING)
        __asm__ volatile ("hlt");
}

void task_block() {
    tasks[current_task].status = TASK_BLOCKED;
    __asm__ volatile ("sti");
    while (tasks[current_task].status == TASK_BLOCKED)
        __asm__ volatile ("hlt");
}

void task_unblock(int id) {
    if (id >= 0 && id < MAX_TASKS && tasks[id].used &&
        tasks[id].status == TASK_BLOCKED)
        tasks[id].status = TASK_RUNNING;
}

void task_check_sleepers() {
    uint32_t now = get_ticks();
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].used &&
            tasks[i].status == TASK_SLEEPING &&
            now >= tasks[i].wake_tick)
            tasks[i].status = TASK_RUNNING;
    }
}

void task_yield() {
    if (tasks[current_task].status == TASK_RUNNING)
        tasks[current_task].ticks = 1;
}

int task_get_status(int id) {
    if (id < 0 || id >= MAX_TASKS) return -1;
    return tasks[id].status;
}