/* task.c — Penjadwal task (priority weighted round-robin) + task switching */
#include "task.h"
#include "timer.h"
#include "vmm.h"
#include "tss.h"
#include "paging.h"
#include "memory.h"

static void str_copy_n(char *dst, const char *src, int n) {
    int i;
    for (i = 0; i < n - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static Task tasks[MAX_TASKS];
/* Stack setiap task dialokasikan dari heap di task_init() — tidak di BSS
 * agar BSS (dimulai ~0x32000) tidak meluap ke area VGA 0xA0000+. */
static uint8_t *stacks_base;
static int current_task = 0;
static int task_count   = 0;
uint32_t *current_esp;  /* ditulis task_switch, dibaca irq0 untuk simpan ESP lama */
uint32_t *next_esp;     /* ditulis task_switch, dibaca irq0 untuk load ESP baru */

void task_init() {
    int i;
    /* Alokasikan pool stack dari heap (MAX_TASKS × STACK_SIZE bytes). */
    stacks_base = (uint8_t*)malloc((uint32_t)(MAX_TASKS * STACK_SIZE));
    for (i = 0; i < MAX_TASKS; i++){
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
    tasks[0].priority  = 3;  /* shell: prioritas tinggi */
    tasks[0].ticks     = 3;
    tasks[0].pipe_id   = -1;
    /* Simpan page directory kernel asli. Wajib agar saat task switch kembali ke
     * task 0, CR3 dikembalikan ke PD yang memiliki mapping VBE LFB (0xE0000000).
     * Tanpa ini, task switch ke bg_task mengubah CR3 ke PD baru,
     * dan saat kembali ke shell, 0xE0000000 tidak ter-map → page fault. */
    tasks[0].page_dir = page_directory;
    str_copy_n(tasks[0].name, "[shell]", 32);
}

 int task_create(void (*entry)()){
    if (task_count >= MAX_TASKS){
        return -1;
    }

    int id = task_count++;
    tasks[id].used      = 1;
    tasks[id].status    = TASK_RUNNING;
    tasks[id].wake_tick = 0;
    tasks[id].priority  = 1;  /* kernel task: prioritas rendah */
    tasks[id].ticks     = 1;
    tasks[id].pipe_id   = -1;
    str_copy_n(tasks[id].name, "[idle]", 32);
    tasks[id].page_dir = vmm_create_page_dir();

    /* Inisialisasi stack: susun frame seperti sisa irq0 (pusha + ds + iret). */
    uint32_t *stack_top = (uint32_t*)(stacks_base + (uint32_t)(id) * STACK_SIZE + STACK_SIZE);

    /* iret frame */
    *(--stack_top) = 0x202;              /* eflags: IF=1 */
    *(--stack_top) = 0x08;               /* cs: kernel code */
    *(--stack_top) = (uint32_t)entry;    /* eip */
    /* pusha: eax ecx edx ebx esp ebp esi edi */
    *(--stack_top) = 0; *(--stack_top) = 0; *(--stack_top) = 0; *(--stack_top) = 0;
    *(--stack_top) = 0; *(--stack_top) = 0; *(--stack_top) = 0; *(--stack_top) = 0;
    *(--stack_top) = 0x10;               /* ds: kernel data (pop eax di irq0) */

    tasks[id].esp = (uint32_t)stack_top;
    return id;
 }

 void task_switch() {
    /* Selalu clear dulu — irq0 skip save/load jika NULL */
    extern uint32_t *current_esp;
    extern uint32_t *next_esp;
    current_esp = 0;
    next_esp    = 0;

    if (task_count < 2) return;

    /* Priority weighted round-robin:
     * Jika task sekarang masih punya sisa ticks dan status RUNNING, tetap di sini. */
    if (tasks[current_task].used &&
        tasks[current_task].status == TASK_RUNNING &&
        tasks[current_task].ticks > 1) {
        tasks[current_task].ticks--;
        return;
    }

    /* Reset ticks task lama */
    if (tasks[current_task].used && tasks[current_task].status == TASK_RUNNING) {
        tasks[current_task].ticks = tasks[current_task].priority;
    }

    /* Cari task RUNNING berikutnya (round-robin) */
    int next = (current_task + 1) % task_count;
    int i;
    for (i = 0; i < task_count; i++) {
        if (tasks[next].used && tasks[next].status == TASK_RUNNING) break;
        next = (next + 1) % task_count;
    }
    if (!tasks[next].used || tasks[next].status != TASK_RUNNING) return;
    if (next == current_task) return;

    current_esp  = &tasks[current_task].esp;
    next_esp     = &tasks[next].esp;
    current_task = next;

    /* Update TSS agar interrupt dari ring 3 menggunakan kernel stack yang benar */
    tss_set_kernel_stack((uint32_t)(stacks_base + (uint32_t)(next) * STACK_SIZE + STACK_SIZE));

    /* Ganti page directory ke milik task berikutnya */
    extern void vmm_switch_dir(uint32_t*);
    if (tasks[current_task].page_dir) {
        vmm_switch_dir(tasks[current_task].page_dir);
    }
 }
 
int task_create_user(uint32_t entry, uint32_t *page_dir, uint32_t user_esp, const char *name) {
    /* Cari slot bebas (used=0) agar slot lama bisa dipakai ulang */
    int id = -1;
    int i;
    for (i = 1; i < MAX_TASKS; i++) {  /* slot 0 = shell, tidak boleh disentuh */
        if (!tasks[i].used) { id = i; break; }
    }
    if (id == -1) return -1;
    if (id >= task_count) task_count = id + 1;
    tasks[id].used      = 1;
    tasks[id].status    = TASK_RUNNING;
    tasks[id].wake_tick = 0;
    tasks[id].page_dir  = page_dir;
    tasks[id].priority  = 2;  /* user program: prioritas normal */
    tasks[id].ticks     = 2;
    tasks[id].pipe_id   = -1;
    str_copy_n(tasks[id].name, name ? name : "?", 32);

    uint32_t *stack_top = (uint32_t*)(stacks_base + (uint32_t)(id) * STACK_SIZE + STACK_SIZE);

    /* iret frame untuk transisi ke ring 3:
     * CPU pop eip, cs, eflags, esp_user, ss saat iret ke privilege lebih rendah */
    *(--stack_top) = 0x23;       /* ss:  user data, ring 3 */
    *(--stack_top) = user_esp;   /* esp: user stack pointer */
    *(--stack_top) = 0x202;      /* eflags: IF=1 */
    *(--stack_top) = 0x1B;       /* cs:  user code, ring 3 */
    *(--stack_top) = entry;      /* eip: entry point ELF */
    /* pusha: 8 register umum (di-restore popa di irq0) */
    *(--stack_top) = 0; *(--stack_top) = 0; *(--stack_top) = 0; *(--stack_top) = 0;
    *(--stack_top) = 0; *(--stack_top) = 0; *(--stack_top) = 0; *(--stack_top) = 0;
    *(--stack_top) = 0x23;       /* ds: user data (di-pop pertama oleh irq0) */
    tasks[id].esp = (uint32_t)stack_top;
    return id;
}

 void task_exit() {
    /* Simpan page_dir lama sebelum kita clear slot */
    uint32_t *dir = tasks[current_task].page_dir;
    tasks[current_task].used      = 0;
    tasks[current_task].page_dir  = 0;

    /* Kembali ke page directory kernel dulu agar aman mem-free page_dir user */
    extern uint32_t page_directory[];
    vmm_switch_dir(page_directory);

    /* Bebaskan semua frame user: page tables + data frames */
    vmm_free_user_memory(dir);

    __asm__ volatile ("sti");
    while (1) {
        __asm__ volatile ("hlt");
    }
 }

/* --- Accessor untuk ps/kill/setprio ---------------------------------------- */
int task_get_max()         { return MAX_TASKS; }
int task_is_used(int id)   { return (id >= 0 && id < MAX_TASKS) ? tasks[id].used : 0; }
const char *task_get_name(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].name : ""; }
int task_get_current()     { return current_task; }
int task_get_priority(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].priority : 0; }
int task_set_priority(int id, int prio) {
    if (id < 0 || id >= MAX_TASKS || !tasks[id].used) return 0;
    if (prio < 1 || prio > 3) return 0;
    tasks[id].priority = (uint8_t)prio;
    tasks[id].ticks    = (uint8_t)prio;  /* reset counter sekarang */
    return 1;
}

/* Aktifkan lagi pipe association untuk task */
void task_set_pipe(int id, int pipe_id) {
    if (id >= 0 && id < MAX_TASKS)
        tasks[id].pipe_id = pipe_id;
}

int task_get_current_pipe() {
    return tasks[current_task].pipe_id;
}

/* kill: matikan task id (tidak boleh kill id 0 = shell) */
int task_kill(int id) {
    if (id <= 0 || id >= MAX_TASKS) return 0;  /* id 0 = shell, lindungi */
    if (!tasks[id].used) return 0;
    /* Tandai tidak aktif dulu agar task_switch tidak menjadwalkannya */
    uint32_t *dir = tasks[id].page_dir;
    tasks[id].used      = 0;
    tasks[id].page_dir  = 0;
    /* Bebaskan semua frame user */
    vmm_free_user_memory(dir);
    if (id == task_count - 1) task_count--;
    return 1;
}

/* Kembalikan alamat puncak kernel stack task id (untuk TSS esp0) */
uint32_t task_get_esp0(int id) {
    if (id < 0 || id >= MAX_TASKS) return 0;
    return (uint32_t)(stacks_base + (uint32_t)(id) * STACK_SIZE + STACK_SIZE);
}

/* Tidurkan task saat ini selama ms milidetik (100Hz = 10ms per tick) */
void task_sleep(uint32_t ms) {
    uint32_t wait_ticks = (ms * 100 + 999) / 1000; /* ceiling division */
    if (wait_ticks == 0) wait_ticks = 1;
    tasks[current_task].status    = TASK_SLEEPING;
    tasks[current_task].wake_tick = get_ticks() + wait_ticks;
    /* aktifkan interrupt agar timer_handler bisa membangunkan kita */
    __asm__ volatile ("sti");
    while (tasks[current_task].status == TASK_SLEEPING) {
        __asm__ volatile ("hlt");
    }
}

/* Blokir task saat ini sampai task_unblock(id) dipanggil */
void task_block() {
    tasks[current_task].status = TASK_BLOCKED;
    __asm__ volatile ("sti");
    while (tasks[current_task].status == TASK_BLOCKED) {
        __asm__ volatile ("hlt");
    }
}

/* Bangunkan task yang diblokir dengan TASK_BLOCKED */
void task_unblock(int id) {
    if (id >= 0 && id < MAX_TASKS && tasks[id].used &&
        tasks[id].status == TASK_BLOCKED)
        tasks[id].status = TASK_RUNNING;
}

/* Bangunkan task sleeping yang wake_tick-nya sudah tercapai — dipanggil timer_handler */
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

/* Paksa switch pada tick berikutnya dengan mengosongkan sisa ticks */
void task_yield() {
    if (tasks[current_task].status == TASK_RUNNING)
        tasks[current_task].ticks = 1;
}

/* Kembalikan status task (TASK_RUNNING / TASK_SLEEPING / TASK_BLOCKED) */
int task_get_status(int id) {
    if (id < 0 || id >= MAX_TASKS) return -1;
    return tasks[id].status;
}