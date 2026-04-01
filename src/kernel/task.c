/* task.c — Penjadwal task (priority weighted round-robin, multi-core) */
#include "task.h"
#include "timer.h"
#include "vmm.h"
#include "tss.h"
#include "memory.h"
#include "spinlock.h"
#include "vfs.h"

static void str_copy_n(char *dst, const char *src, int n) {
    int i;
    for (i = 0; i < n - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static Task tasks[MAX_TASKS];
static uint8_t *stacks_base;
static int current_task = 0;
static int task_count   = 0;

/* Spinlock: melindungi cpu_id field saat claim/release task oleh BSP dan AP. */
static volatile int task_lock = 0;

/* Set 1 oleh smp_init setelah AP online — BSP tidak boleh ambil is_user==0 task */
static volatile int has_ap = 0;
void task_set_has_ap(int v) { has_ap = v; }

/* Per-AP scheduler state (untuk 1 AP; diperluas ke array jika CPU > 2). */
static int current_task_ap = -1;  /* task yg sedang jalan di AP, -1 = belum ada */
uint64_t *ap_current_rsp   = 0;   /* dibaca lapic_timer_isr (asm) */
uint64_t *ap_next_rsp      = 0;   /* dibaca lapic_timer_isr (asm) */

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
    tasks[0].cpu_id    = 0;    /* shell: selalu di BSP */
    tasks[0].is_user   = 1;    /* pin ke BSP, AP tidak boleh ambil */
    /* Gunakan boot PML4 di alamat fisik 0x1000 (identity mapped = VA 0x1000) */
    tasks[0].page_dir  = (uint64_t *)0x1000;
    str_copy_n(tasks[0].name, "[shell]", 32);
}

int task_create(void (*entry)()) {
    /* Scan slot bebas (skip slot 0 = shell) — bukan sequential increment */
    int id = -1, i;
    for (i = 1; i < MAX_TASKS; i++) {
        if (!tasks[i].used) { id = i; break; }
    }
    if (id == -1) return -1;
    if (id >= task_count) task_count = id + 1;
    tasks[id].used      = 1;
    tasks[id].status    = TASK_RUNNING;
    tasks[id].wake_tick = 0;
    tasks[id].priority  = 1;
    tasks[id].ticks     = 1;
    tasks[id].pipe_id   = -1;
    tasks[id].waiter    = -1;
    tasks[id].heap_end  = 0;  /* kernel task: tidak punya user heap */
    /* Tahap M: load balance — kernel task di-assign langsung ke AP jika ada */
    tasks[id].cpu_id    = has_ap ? (int8_t)1 : (int8_t)-1;
    tasks[id].is_user   = 0;   /* ring-0 kernel task */
    tasks[id].is_thread = 0;
    tasks[id].parent_tid = -1;
    str_copy_n(tasks[id].name, "[idle]", 32);
    tasks[id].page_dir  = vmm_create_page_dir();

    /* Susun stack awal:
     * Di 64-bit mode, iretq SELALU pop 5 item: RIP, CS, RFLAGS, RSP, SS
     * (bahkan untuk same-privilege ring-0 → ring-0).
     *   [tinggi] SS, RSP, RFLAGS, CS, RIP  (iretq frame, 5 qword)
     *   [rendah] 15 x 0                    (GPR slots: rax..r15) */
    uint64_t stack_base = (uint64_t)(stacks_base + (uint64_t)id * STACK_SIZE + STACK_SIZE);
    uint64_t *stack_top = (uint64_t *)stack_base;
    *(--stack_top) = 0x10;                /* SS: kernel data segment */
    *(--stack_top) = stack_base;          /* RSP: top of task's own stack */
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
    tasks[id].cpu_id    = -1;  /* ring-3 user task: dimulai bebas, BSP akan klaim */
    tasks[id].waiter    = -1;
    tasks[id].heap_end  = 0x400000ULL; /* user heap mulai di 0x400000 */
    tasks[id].is_user   = 1;
    tasks[id].is_thread = 0;
    tasks[id].parent_tid = -1;
    str_copy_n(tasks[id].name, name ? name : "?", 32);

    /* Susun stack awal:
     *   [tinggi] SS, RSP_user, RFLAGS, CS_user, RIP  (iretq frame ring-0->ring-3, 5 qword)
     *   [rendah] 15 x 0                              (GPR slots) */
    uint64_t *stack_top = (uint64_t *)(stacks_base + (uint64_t)id * STACK_SIZE + STACK_SIZE);
    *(--stack_top) = 0x23;              /* SS: user data selector */
    *(--stack_top) = user_rsp;          /* RSP: user mode stack */
    *(--stack_top) = 0x202;             /* RFLAGS: IF=1 */
    *(--stack_top) = 0x2B;              /* CS: user code selector (GDT 0x28 | RPL=3) */
    *(--stack_top) = entry;             /* RIP: entry point ELF */
    int k;
    for (k = 0; k < 15; k++) *(--stack_top) = 0;

    tasks[id].rsp = (uint64_t)stack_top;
    /* Tahap J: setup fd 0/1/2 untuk task ring-3 */
    vfs_init_task(id);
    return id;
}

/* -----------------------------------------------------------------------
 * task_create_thread — buat user thread yang berbagi page_dir dengan induk.
 *
 * entry       : alamat virtual fungsi thread (ring-3)
 * arg         : nilai yang diteruskan ke rdi saat thread pertama kali jalan
 * parent_tid  : tid proses pemilik page_dir (biasanya task saat ini)
 *
 * Stack thread user ring-3 dialokasikan di VA  0x700000 + id*0x1000 (4KB).
 * RSP awal = 0x700000 + id*0x1000 + 0x1000 (atas halaman, tumbuh ke bawah).
 * [RSP] = 0  (fake return addr — thread HARUS panggil thread_exit(), tidak    boleh return biasa)
 * ----------------------------------------------------------------------- */
int task_create_thread(uint64_t entry, uint64_t arg, int parent_tid) {
    int id = -1, i;
    for (i = 1; i < MAX_TASKS; i++) {
        if (!tasks[i].used) { id = i; break; }
    }
    if (id == -1) return -1;
    if (id >= task_count) task_count = id + 1;

    if (parent_tid < 0 || parent_tid >= MAX_TASKS || !tasks[parent_tid].used)
        return -1;

    tasks[id].used       = 1;
    tasks[id].status     = TASK_RUNNING;
    tasks[id].wake_tick  = 0;
    tasks[id].page_dir   = tasks[parent_tid].page_dir;  /* berbagi address space */
    tasks[id].priority   = 2;
    tasks[id].ticks      = 2;
    tasks[id].pipe_id    = -1;
    tasks[id].cpu_id     = -1;
    tasks[id].waiter     = -1;
    tasks[id].heap_end   = tasks[parent_tid].heap_end;  /* thread lihat heap yang sama */
    tasks[id].is_user    = 1;
    tasks[id].is_thread  = 1;
    tasks[id].parent_tid = parent_tid;
    str_copy_n(tasks[id].name, "thread", 32);

    /* Alokasikan 1 halaman user stack ring-3 untuk thread ini di VA 0x700000+id*0x1000.
     * Setiap slot task punya halaman stack sendiri sehingga tidak tabrakan. */
    uint64_t tstack_va = 0x700000ULL + (uint64_t)id * 0x1000ULL;
    uint64_t tstack_frame = pmm_alloc_frame();
    if (!tstack_frame) { tasks[id].used = 0; return -1; }
    vmm_map_page(tasks[id].page_dir, tstack_va, tstack_frame, 7);  /* P+RW+User */
    uint64_t user_rsp = tstack_va + 0x1000ULL;  /* puncak halaman */

    /* Susun kernel stack awal (sama seperti task_create_user):
     *   [tinggi] SS, RSP_user, RFLAGS, CS, RIP   iretq frame
     *   [rendah] 15 GPR slot
     * Slot k=5 (rdi) diisi dengan arg agar thread_fn(arg) diterima benar */
    uint64_t *stack_top = (uint64_t *)(stacks_base + (uint64_t)id * STACK_SIZE + STACK_SIZE);
    *(--stack_top) = 0x23;               /* SS */
    *(--stack_top) = user_rsp;           /* RSP user */
    *(--stack_top) = 0x202;              /* RFLAGS: IF=1 */
    *(--stack_top) = 0x2B;              /* CS user (GDT 0x28 | RPL=3) */
    *(--stack_top) = entry;              /* RIP */
    int k;
    /* k=5 corresponds to rdi (lihat komentar SAVE_REGS di isr.asm):
     * push order: rax(k=0) rbx(k=1) rcx(k=2) rdx(k=3) rsi(k=4) rdi(k=5) ... */
    for (k = 0; k < 15; k++) *(--stack_top) = (k == 5) ? arg : 0ULL;

    tasks[id].rsp = (uint64_t)stack_top;
    return id;
}

void task_switch() {
    current_rsp = 0;
    next_rsp    = 0;

    if (task_count < 2) return;

    /* Tick countdown: task masih punya jatah, jangan switch */
    if (tasks[current_task].used &&
        tasks[current_task].status == TASK_RUNNING &&
        tasks[current_task].ticks > 1) {
        tasks[current_task].ticks--;
        return;
    }

    /* Reset ticks task yang selesai jatah-nya */
    if (tasks[current_task].used && tasks[current_task].status == TASK_RUNNING)
        tasks[current_task].ticks = tasks[current_task].priority;

    /* Cari task berikutnya dengan spinlock agar tidak race dengan AP */
    spinlock_acquire(&task_lock);

    int next = (current_task + 1) % MAX_TASKS;
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        /* Jika ada AP, BSP hanya ambil task is_user==1 (shell/ring-3).
         * Task is_user==0 (kernel task) dibiarkan bebas untuk AP. */
        if (tasks[next].used &&
            tasks[next].status == TASK_RUNNING &&
            tasks[next].cpu_id == -1 &&
            (!has_ap || tasks[next].is_user == 1)) break;
        next = (next + 1) % MAX_TASKS;
    }

    if (!tasks[next].used || tasks[next].status != TASK_RUNNING ||
        tasks[next].cpu_id != -1 || next == current_task) {
        spinlock_release(&task_lock);
        return;
    }

    /* Lepas task lama (kembalikan ke pool), klaim task baru untuk BSP */
    tasks[current_task].cpu_id = (int8_t)-1;
    tasks[next].cpu_id         = (int8_t)0;

    spinlock_release(&task_lock);

    current_rsp  = &tasks[current_task].rsp;
    next_rsp     = &tasks[next].rsp;
    current_task = next;

    uint64_t kstack_top = (uint64_t)(stacks_base + (uint64_t)next * STACK_SIZE + STACK_SIZE);
    tss64_set_kernel_stack(kstack_top);

    /* D1: update kernel stack pointer untuk syscall_entry */
    extern volatile uint64_t syscall_kstack;
    syscall_kstack = kstack_top;

    if (tasks[current_task].page_dir)
        vmm_switch_dir(tasks[current_task].page_dir);
}

/* ------------------------------------------------------------------
 * task_switch_ap — scheduler untuk Application Processor (AP).
 *
 * Dipanggil dari lapic_timer_isr pada setiap AP timer tick.
 * Menggunakan work stealing: ambil task ring-0 (is_user==0) yang
 * tidak sedang dijalankan oleh CPU manapun (cpu_id==-1).
 *
 * Setelah fungsi ini kembali, lapic_timer_isr membaca ap_current_rsp
 * dan ap_next_rsp untuk melakukan context switch.
 * ------------------------------------------------------------------ */
void task_switch_ap(int cpu_idx)
{
    ap_current_rsp = 0;
    ap_next_rsp    = 0;

    if (task_count < 1) return;

    /* Tick countdown AP — hanya AP yang akses current_task_ap, tanpa lock */
    if (current_task_ap >= 0 && tasks[current_task_ap].used &&
        tasks[current_task_ap].status == TASK_RUNNING &&
        tasks[current_task_ap].ticks > 1) {
        tasks[current_task_ap].ticks--;
        return;
    }

    /* Reset ticks */
    if (current_task_ap >= 0 && tasks[current_task_ap].used &&
        tasks[current_task_ap].status == TASK_RUNNING)
        tasks[current_task_ap].ticks = tasks[current_task_ap].priority;

    /* Cari task ring-0 bebas dengan spinlock (work stealing) */
    spinlock_acquire(&task_lock);

    int start = (current_task_ap >= 0) ? (current_task_ap + 1) % MAX_TASKS : 0;
    int next = start;
    int found = 0, i;

    /* Tahap M: first pass — cari task yang di-assign langsung ke AP ini */
    for (i = 0; i < MAX_TASKS; i++) {
        if (tasks[next].used &&
            tasks[next].status == TASK_RUNNING &&
            tasks[next].cpu_id == (int8_t)cpu_idx &&
            tasks[next].is_user == 0 &&
            next != current_task_ap) {
            found = 1;
            break;
        }
        next = (next + 1) % MAX_TASKS;
    }

    /* Second pass — work-steal: kernel task bebas (cpu_id==-1) */
    if (!found) {
        next = start;
        for (i = 0; i < MAX_TASKS; i++) {
            if (tasks[next].used &&
                tasks[next].status == TASK_RUNNING &&
                tasks[next].cpu_id == -1 &&
                tasks[next].is_user == 0) {
                found = 1;
                break;
            }
            next = (next + 1) % MAX_TASKS;
        }
    }

    if (!found || next == current_task_ap) {
        /* Tidak ada task baru — pertahankan task saat ini (re-klaim) */
        if (current_task_ap >= 0 && tasks[current_task_ap].used)
            tasks[current_task_ap].cpu_id = (int8_t)cpu_idx;
        spinlock_release(&task_lock);
        return;
    }

    /* Lepas task lama, klaim task baru */
    if (current_task_ap >= 0 && tasks[current_task_ap].used)
        tasks[current_task_ap].cpu_id = (int8_t)-1;
    tasks[next].cpu_id = (int8_t)cpu_idx;

    spinlock_release(&task_lock);

    /* Set pointer RSP untuk context switch di asm */
    if (current_task_ap >= 0 && tasks[current_task_ap].used)
        ap_current_rsp = &tasks[current_task_ap].rsp;
    ap_next_rsp    = &tasks[next].rsp;
    current_task_ap = next;

    /* Update TSS RSP0 AP agar exception di ring-0 punya stack bersih */
    uint64_t kstack = (uint64_t)(stacks_base + (uint64_t)next * STACK_SIZE + STACK_SIZE);
    tss64_set_kernel_stack_cpu(cpu_idx, kstack);
}

void task_exit() {
    int tid = current_task;
    uint8_t is_thr  = tasks[tid].is_thread;
    uint64_t *dir   = tasks[tid].page_dir;

    /* Thread tidak memiliki page_dir sendiri → jangan tutup fd dan jangan free page_dir.
     * Proses biasa: tutup semua fd lalu bebaskan seluruh memori user. */
    if (!is_thr) {
        /* Tahap J: tutup semua fd milik task ini */
        vfs_close_all(tid);
    }

    /* Simpan waiter sebelum membersihkan slot */
    int waiter = tasks[tid].waiter;

    tasks[tid].used      = 0;
    tasks[tid].cpu_id    = (int8_t)-1;   /* kembalikan ke pool */
    tasks[tid].page_dir  = 0;
    tasks[tid].waiter    = -1;
    tasks[tid].is_thread = 0;

    /* Kembali ke boot PML4 dulu */
    vmm_switch_dir((uint64_t *)0x1000);

    if (!is_thr) {
        /* Hanya proses pemilik yang membebaskan page_dir */
        vmm_free_user_memory(dir);
    }

    /* Bangunkan task yang sedang menunggu task ini selesai */
    if (waiter >= 0 && waiter < MAX_TASKS && tasks[waiter].used)
        task_unblock(waiter);

    __asm__ volatile ("sti");
    while (1) __asm__ volatile ("hlt");
}

int task_get_max()       { return MAX_TASKS; }
int task_get_count()     { return task_count; }
int task_is_used(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].used : 0; }
const char *task_get_name(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].name : ""; }
int task_get_current() { return current_task; }
int task_get_cpu(int id) { return (id >= 0 && id < MAX_TASKS) ? (int)tasks[id].cpu_id : -1; }
int task_get_priority(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].priority : 0; }

uint64_t task_get_heap_end(int id) {
    return (id >= 0 && id < MAX_TASKS) ? tasks[id].heap_end : 0;
}
void task_set_heap_end(int id, uint64_t end) {
    if (id >= 0 && id < MAX_TASKS) tasks[id].heap_end = end;
}

void task_wait(int tid) {
    if (tid <= 0 || tid >= MAX_TASKS) return;
    if (!tasks[tid].used) return;  /* sudah selesai */
    tasks[tid].waiter = current_task;  /* daftarkan minat */
    task_block();  /* tidur sampai task_exit membangunkan kita */
    if (tid < MAX_TASKS) tasks[tid].waiter = -1;  /* bersihkan */
}

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

uint64_t *task_get_page_dir(int id) {
    if (id < 0 || id >= MAX_TASKS || !tasks[id].used) return 0;
    return tasks[id].page_dir;
}

void task_sleep(uint32_t ms) {
    /* Timer sekarang 1000 Hz, 1 tick = 1 ms */
    uint32_t wait_ticks = (ms > 0) ? ms : 1;
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