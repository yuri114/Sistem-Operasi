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

/* F-T: Kode exit per-slot; diisi saat task_exit_code, dibaca oleh SYS_WAITPID. */
static int exit_codes[MAX_TASKS];

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
    tasks[id].fpu_valid = 0;
    tasks[id].itimer_interval  = 0;
    tasks[id].itimer_remaining = 0;
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

int task_create_user(uint64_t entry, uint64_t *page_dir, uint64_t user_rsp, const char *name,
                     int argc, const char *const argv[]) {
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
    tasks[id].cpu_id    = -1;
    tasks[id].waiter    = -1;
    tasks[id].heap_end  = 0x400000ULL;
    tasks[id].is_user   = 1;
    tasks[id].fpu_valid = 0;
    tasks[id].itimer_interval  = 0;
    tasks[id].itimer_remaining = 0;
    tasks[id].is_thread = 0;
    tasks[id].parent_tid = -1;
    str_copy_n(tasks[id].name, name ? name : "?", 32);

    /* Simpan argv di task struct (diambil user via SYS_GETARGV syscall).
     * Format arg_buf: "arg0\0arg1\0arg2\0" (null-terminated strings berurutan). */
    tasks[id].arg_argc = 0;
    tasks[id].arg_buf[0] = '\0';
    {
        int pos = 0;
        int k;
        int ac = (argc > 8) ? 8 : argc;
        for (k = 0; k < ac && pos < 510; k++) {
            const char *s = (argv && argv[k]) ? argv[k] : "";
            int si = 0;
            while (s[si] && pos < 510) tasks[id].arg_buf[pos++] = s[si++];
            tasks[id].arg_buf[pos++] = '\0';
        }
        tasks[id].arg_buf[pos] = '\0';  /* extra null terminator */
        tasks[id].arg_argc = ac;
    }

    /* Susun kernel stack awal:
     *   [tinggi] SS, RSP_user, RFLAGS, CS_user, RIP  (iretq frame, 5 qword)
     *   [rendah] 15 x 0                              (GPR slots: r15..rax)
     *
     * GPR slot layout (stack_top pointer ke elemen terendah setelah loop):
     *   [0]=r15 [1]=r14 [2]=r13 [3]=r12 [4]=r11 [5]=r10 [6]=r9  [7]=r8
     *   [8]=rbp [9]=rdi [10]=rsi [11]=rdx [12]=rcx [13]=rbx [14]=rax
     * (urutan sesuai RESTORE_REGS di isr.asm: pop r15,r14,...,rbp,rdi,rsi,...,rax) */
    uint64_t *stack_top = (uint64_t *)(stacks_base + (uint64_t)id * STACK_SIZE + STACK_SIZE);
    *(--stack_top) = 0x23;        /* SS: user data selector */
    *(--stack_top) = user_rsp;    /* RSP: user mode stack */
    *(--stack_top) = 0x202;       /* RFLAGS: IF=1 */
    *(--stack_top) = 0x2B;        /* CS: user code selector */
    *(--stack_top) = entry;       /* RIP: entry point */
    {
        int k;
        for (k = 0; k < 15; k++) *(--stack_top) = 0;
        /* rdi (slot 9) = argc — convenience agar program bisa baca tanpa syscall */
        stack_top[9] = (uint64_t)(unsigned int)tasks[id].arg_argc;
    }

    tasks[id].rsp = (uint64_t)stack_top;

    /* F-U2+: alokasi TLS untuk proses user (sama dengan thread, tapi di page_dir sendiri).
     * Offset 0 = self-pointer (ABI standar), offset 8 = errno per-proses. */
    {
        uint64_t tls_va   = 0x800000ULL + (uint64_t)id * 0x1000ULL;
        uint64_t tls_phys = pmm_alloc_frame();
        uint8_t *tp = (uint8_t *)(uintptr_t)tls_phys;
        int zi; for (zi = 0; zi < 4096; zi++) tp[zi] = 0;
        /* Tulis self-pointer di offset 0 */
        *(uint64_t *)tp = tls_va;
        vmm_map_page(page_dir, tls_va, tls_phys, 7); /* u/s, r/w, present */
        tasks[id].tls_frame = tls_phys;
        /* IA32_FS_BASE akan di-set oleh task_switch pertama kali task ini jalan */
    }

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
 * Stack thread user ring-3 dialokasikan di VA  0x700000 + id*0x5000 (16KB).
 * Slot per task: 5 halaman — halaman 0 (guard, tidak dipetakan) + 4 halaman data.
 * RSP awal = 0x700000 + id*0x5000 + 0x5000 (puncak slot, tumbuh ke bawah).
 * [RSP] = 0  (fake return addr — thread HARUS panggil thread_exit(), tidak boleh return biasa)
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
    tasks[id].fpu_valid  = 0;
    tasks[id].itimer_interval  = 0;
    tasks[id].itimer_remaining = 0;
    str_copy_n(tasks[id].name, "thread", 32);

    /* F-Q3: Demand paging untuk thread stack.
     * Slot VA: 0x700000 + id*0x5000  → 5 halaman per slot.
     *   Halaman 0 (guard): tidak dipetakan → #PF = stack overflow.
     *   Halaman 1-4: TIDAK dipetakan sekarang → demand paged saat diakses pertama kali.
     * Tidak ada pmm_alloc_frame() di sini; tstack_frames[] dibersihkan ke 0. */
    int k;
    for (k = 0; k < 4; k++) tasks[id].tstack_frames[k] = 0;

    uint64_t tstack_va = 0x700000ULL + (uint64_t)id * 0x5000ULL;
    uint64_t user_rsp  = tstack_va + 0x5000ULL;  /* puncak slot */

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
    /* k=5 corresponds to rdi (lihat komentar SAVE_REGS di isr.asm):
     * push order: rax(k=0) rbx(k=1) rcx(k=2) rdx(k=3) rsi(k=4) rdi(k=5) ... */
    for (k = 0; k < 15; k++) *(--stack_top) = (k == 5) ? arg : 0ULL;

    tasks[id].rsp = (uint64_t)stack_top;

    /* F-U2: alokasi halaman TLS dan petakan di VA 0x800000 + id*0x1000.
     * FS MSR (IA32_FS_BASE = 0xC0000100) disetel ke VA tersebut agar
     * instruksi 'mov %%fs:offset, reg' bekerja dari user space thread. */
    {
        uint64_t tls_va   = 0x800000ULL + (uint64_t)id * 0x1000ULL;
        uint64_t tls_phys = pmm_alloc_frame();
        /* zero-fill halaman TLS */
        uint8_t *tp = (uint8_t *)(tls_phys);
        int zi; for (zi = 0; zi < 4096; zi++) tp[zi] = 0;
        vmm_map_page(tasks[parent_tid].page_dir, tls_va, tls_phys, 7); /* u/s, r/w, p */
        tasks[id].tls_frame = tls_phys;
        /* Tulis IA32_FS_BASE MSR: ECX=0xC0000100, EDX:EAX = tls_va */
        uint32_t lo = (uint32_t)tls_va;
        uint32_t hi = (uint32_t)(tls_va >> 32);
        __asm__ volatile ("wrmsr" :: "c"(0xC0000100u), "a"(lo), "d"(hi));
    }
    vfs_copy_fds(parent_tid, id);   /* thread warisi fd dari parent (pipe, net, file) */
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

    /* AW: priority-based selection dengan aging.
     * effective_priority = (priority * 4) - nice + (age_ticks / 8)
     * Lebih tinggi = lebih diutamakan.                              */
    int best = -1;
    int best_score = -9999;
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].used) continue;
        if (tasks[i].status != TASK_RUNNING) continue;
        if (tasks[i].cpu_id != -1) continue;
        if (i == current_task) continue;
        if (has_ap && tasks[i].is_user == 0) continue;
        int score = (int)tasks[i].priority * 4 - (int)tasks[i].nice
                    + (int)(tasks[i].age_ticks >> 3);
        if (score > best_score) { best_score = score; best = i; }
    }

    /* Increment age_ticks for all eligible tasks that were NOT chosen */
    for (i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].used) continue;
        if (tasks[i].status != TASK_RUNNING) continue;
        if (tasks[i].cpu_id != -1) continue;
        if (i == best) continue;
        if (i == current_task) continue;
        if (tasks[i].age_ticks < 0xFFFFFFFFu) tasks[i].age_ticks++;
    }

    int next = best;

    if (next < 0 || !tasks[next].used || tasks[next].status != TASK_RUNNING ||
        tasks[next].cpu_id != -1 || next == current_task) {
        spinlock_release(&task_lock);
        return;
    }

    /* Lepas task lama (kembalikan ke pool), klaim task baru untuk BSP */
    tasks[current_task].cpu_id = (int8_t)-1;
    tasks[next].cpu_id         = (int8_t)0;
    tasks[next].age_ticks      = 0;  /* AW: reset aging counter */

    spinlock_release(&task_lock);

    current_rsp  = &tasks[current_task].rsp;
    next_rsp     = &tasks[next].rsp;

    /* x87/SSE state: simpan state task lama, restore state task baru */
    int old_task = current_task;
    __asm__ volatile ("fxsave %0" : "=m"(tasks[old_task].fpu_state));
    tasks[old_task].fpu_valid = 1;
    if (tasks[next].fpu_valid) {
        __asm__ volatile ("fxrstor %0" :: "m"(tasks[next].fpu_state));
    } else {
        /* Inisialisasi FPU bersih untuk task baru */
        __asm__ volatile ("fninit");
    }

    current_task = next;

    uint64_t kstack_top = (uint64_t)(stacks_base + (uint64_t)next * STACK_SIZE + STACK_SIZE);
    tss64_set_kernel_stack(kstack_top);

    /* D1: update kernel stack pointer untuk syscall_entry */
    extern volatile uint64_t syscall_kstack;
    syscall_kstack = kstack_top;

    if (tasks[current_task].page_dir)
        vmm_switch_dir(tasks[current_task].page_dir);

    /* F-U2: jika task berikutnya punya TLS (thread atau user task), restore FS MSR */
    if (tasks[current_task].tls_frame) {
        uint64_t tls_va = 0x800000ULL + (uint64_t)current_task * 0x1000ULL;
        uint32_t lo = (uint32_t)tls_va;
        uint32_t hi = (uint32_t)(tls_va >> 32);
        __asm__ volatile ("wrmsr" :: "c"(0xC0000100u), "a"(lo), "d"(hi));
    }
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

/* F-T: task_exit_code(code) — versi task_exit yang menyimpan kode exit.
 * task_exit() sekarang hanya delegate ke sini dengan code=0. */
void task_exit_code(int code) {
    int tid = current_task;
    uint8_t   is_thr    = tasks[tid].is_thread;
    uint64_t *dir       = tasks[tid].page_dir;
    int       ptid      = tasks[tid].parent_tid;
    uint64_t  tstack_va = 0x700000ULL + (uint64_t)tid * 0x5000ULL;
    uint64_t  tstack_fs[4];
    int k;
    for (k = 0; k < 4; k++) tstack_fs[k] = tasks[tid].tstack_frames[k];

    /* Simpan kode exit sebelum membersihkan slot */
    exit_codes[tid] = code;

    /* Thread tidak memiliki page_dir sendiri → jangan tutup fd dan jangan free page_dir.
     * Proses biasa: tutup semua fd lalu bebaskan seluruh memori user. */
    if (!is_thr) {
        /* Tahap J: tutup semua fd milik task ini */
        vfs_close_all(tid);
    }

    /* Simpan waiter sebelum membersihkan slot */
    int waiter = tasks[tid].waiter;

    tasks[tid].used             = 0;
    tasks[tid].cpu_id           = (int8_t)-1;   /* kembalikan ke pool */
    tasks[tid].page_dir         = 0;
    tasks[tid].waiter           = -1;
    tasks[tid].is_thread        = 0;
    tasks[tid].pending_signals  = 0;
    for (k = 0; k < 4; k++) tasks[tid].tstack_frames[k] = 0;

    /* F-U2: bebaskan halaman TLS thread */
    uint64_t tls_frame_saved = tasks[tid].tls_frame;
    tasks[tid].tls_frame = 0;

    /* Kembali ke boot PML4 dulu */
    vmm_switch_dir((uint64_t *)0x1000);

    if (is_thr) {
        /* F-P1/F-Q3: bebaskan frame stack thread.
         * Gunakan vmm_get_phys agar demand-paged frames (F-Q3) juga ikut dibersihkan. */
        if (ptid >= 0 && ptid < MAX_TASKS && tasks[ptid].used) {
            for (k = 0; k < 4; k++) {
                uint64_t page_va = tstack_va + 0x1000ULL + (uint64_t)k * 0x1000ULL;
                uint64_t phys = vmm_get_phys(tasks[ptid].page_dir, page_va);
                if (phys >= 768ULL * PAGE_SIZE) {
                    vmm_unmap_page(tasks[ptid].page_dir, page_va);
                    pmm_free_frame(phys);
                }
            }
            /* F-U2: unmap dan bebaskan halaman TLS thread */
            if (tls_frame_saved) {
                uint64_t tls_va = 0x800000ULL + (uint64_t)tid * 0x1000ULL;
                vmm_unmap_page(tasks[ptid].page_dir, tls_va);
                pmm_free_frame(tls_frame_saved);
            }
        }
    } else {
        /* F-O2: bunuh semua thread yang masih hidup sebelum membebaskan halaman. */
        int i;
        for (i = 1; i < MAX_TASKS; i++) {
            if (tasks[i].used && tasks[i].is_thread && tasks[i].parent_tid == tid) {
                int tw = tasks[i].waiter;
                int j;
                /* F-Q3: gunakan vmm_get_phys agar demand-paged thread stack ikut dibebaskan */
                uint64_t ts_va = 0x700000ULL + (uint64_t)i * 0x5000ULL;
                for (j = 0; j < 4; j++) {
                    uint64_t page_va = ts_va + 0x1000ULL + (uint64_t)j * 0x1000ULL;
                    uint64_t phys = vmm_get_phys(dir, page_va);
                    if (phys >= 768ULL * PAGE_SIZE)
                        pmm_free_frame(phys);
                }
                tasks[i].used   = 0;
                tasks[i].cpu_id = (int8_t)-1;
                /* Bangunkan siapapun yang sedang thread_join ke thread ini */
                if (tw >= 0 && tw < MAX_TASKS && tasks[tw].used)
                    task_unblock(tw);
            }
        }
        /* Hanya proses pemilik yang membebaskan page_dir */
        vmm_free_user_memory(dir);
    }

    /* Bangunkan task yang sedang menunggu task ini selesai */
    if (waiter >= 0 && waiter < MAX_TASKS && tasks[waiter].used)
        task_unblock(waiter);

    __asm__ volatile ("sti");
    while (1) __asm__ volatile ("hlt");
}

void task_exit() {
    task_exit_code(0);
}

/* F-T: Kirim sinyal ke task target. Untuk sinyal terminating:
 * setel bit pending dan bangunkan task agar segera masuk kernel, */
void task_send_signal(int tid, int sig) {
    if (tid <= 0 || tid >= MAX_TASKS || !tasks[tid].used) return;
    if (sig <= 0 || sig >= 32) return;
    tasks[tid].pending_signals |= (1u << sig);
    /* Bangunkan jika tertidur/terblokir agar segera masuk kernel
     * dan signal didelivery di task_check_signals(). */
    if (tasks[tid].status != TASK_RUNNING)
        task_unblock(tid);
}

/* F-T: Cek sinyal terminating untuk task saat ini.
 * Dipanggil di awal syscall_handler dan setelah task_block di pipe_read. */
void task_check_signals(void) {
    int tid = current_task;
    uint32_t term = (1u << SIGINT) | (1u << SIGKILL) | (1u << SIGTERM);
    uint32_t sigs = tasks[tid].pending_signals & term;
    if (!sigs || !tasks[tid].is_user) return;
    /* Cari sinyal dengan bit terendah */
    int sig = 1;
    while (sig < 32 && !(sigs & (1u << sig))) sig++;
    task_exit_code(128 + sig);  /* tidak pernah kembali */
}

/* F-T: Kembalikan exit code task yang sudah selesai. */
int task_get_exit_code(int tid) {
    if (tid < 0 || tid >= MAX_TASKS) return -1;
    return exit_codes[tid];
}

void task_set_name(int id, const char *name) {
    if (id < 0 || id >= MAX_TASKS || !tasks[id].used || !name) return;
    str_copy_n(tasks[id].name, name, 32);
}

int task_get_max()       { return MAX_TASKS; }
int task_get_count()     { return task_count; }
int task_is_used(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].used : 0; }
int task_is_thread(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].is_thread : 0; }
int task_get_parent(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].parent_tid : -1; }
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

/* AW: nice value getter/setter */
int task_get_nice(int id) {
    if (id < 0 || id >= MAX_TASKS || !tasks[id].used) return 0;
    return (int)tasks[id].nice;
}

int task_set_nice(int id, int nice_val) {
    if (id < 0 || id >= MAX_TASKS || !tasks[id].used) return 0;
    if (nice_val < -20) nice_val = -20;
    if (nice_val >  19) nice_val =  19;
    tasks[id].nice = (int8_t)nice_val;
    return 1;
}

/* AY: uid getter/setter */
int task_get_uid(int id) {
    if (id < 0 || id >= MAX_TASKS || !tasks[id].used) return -1;
    return tasks[id].uid;
}

void task_set_uid(int id, int uid) {
    if (id < 0 || id >= MAX_TASKS) return;
    tasks[id].uid = uid;
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

/* Fondasi AA — argv accessors */
int tasks_getargc(int id) {
    if (id < 0 || id >= MAX_TASKS || !tasks[id].used) return 0;
    return tasks[id].arg_argc;
}
const char *tasks_getargbuf(int id) {
    if (id < 0 || id >= MAX_TASKS || !tasks[id].used) return "";
    return tasks[id].arg_buf;
}

void task_set_page_dir(int id, uint64_t *dir) {
    if (id >= 0 && id < MAX_TASKS) tasks[id].page_dir = dir;
}

/*
 * task_create_fork — buat task anak (child) untuk fork().
 *
 * child_rip : VA user-space tempat anak melanjutkan eksekusi (resume setelah syscall)
 * child_rsp : RSP user-space anak (sama persis dengan induk saat fork() dipanggil)
 *
 * Kernel stack anak dibuat dalam format iretq (sama dengan task_create_user),
 * dengan rax=0 agar fork() mengembalikan 0 di proses anak.
 */
int task_create_fork(int parent_tid, uint64_t child_rip, uint64_t child_rsp) {
    int id = -1, i;
    for (i = 1; i < MAX_TASKS; i++) {
        if (!tasks[i].used) { id = i; break; }
    }
    if (id == -1) return -1;
    if (id >= task_count) task_count = id + 1;

    /* Salin seluruh Task struct dari induk, sesuaikan field yang perlu berbeda */
    tasks[id] = tasks[parent_tid];

    tasks[id].used       = 1;
    tasks[id].status     = TASK_RUNNING;
    tasks[id].cpu_id     = (int8_t)-1;   /* bebas, BSP akan klaim */
    tasks[id].waiter     = -1;
    tasks[id].is_thread  = 0;
    tasks[id].parent_tid = -1;           /* child adalah proses mandiri */
    tasks[id].wake_tick  = 0;
    /* page_dir diberi nilai dari caller (child_pml4) setelah vmm_copy_cow */

    /* Susun kernel stack anak dalam format iretq ring-3.
     *
     * Layout GPR di kernel stack induk (relative ke parent_kstack_top setelah SAVE_REGS):
     *   kstack_top-48  = rax (k=0) — syscall num, anak harus 0
     *   kstack_top-56  = rbx (k=1)
     *   kstack_top-64  = rcx (k=2)
     *   kstack_top-72  = rdx (k=3)
     *   kstack_top-80  = rsi (k=4)
     *   kstack_top-88  = rdi (k=5)
     *   kstack_top-96  = rbp (k=6)  ← PENTING: frame pointer
     *   kstack_top-104 = r8  (k=7)
     *   ...
     *   kstack_top-160 = r15 (k=14)
     *
     * Anak mendapat salinan GPR induk (kecuali rax=0) agar frame pointer
     * (rbp) dan semua register lain tetap valid setelah iretq.
     */
    uint64_t parent_kstack_top = (uint64_t)(stacks_base + (uint64_t)parent_tid * STACK_SIZE + STACK_SIZE);
    /* RFLAGS induk (kstack_top-24) — salin agar flag state konsisten */
    uint64_t parent_rflags = *(uint64_t *)(parent_kstack_top - 24);

    uint64_t *stack_top = (uint64_t *)(stacks_base + (uint64_t)id * STACK_SIZE + STACK_SIZE);
    *(--stack_top) = 0x23;           /* SS: user data */
    *(--stack_top) = child_rsp;      /* RSP: user stack (sama dgn induk) */
    *(--stack_top) = parent_rflags;  /* RFLAGS: salin dari induk */
    *(--stack_top) = 0x2B;           /* CS: user code (RPL=3) */
    *(--stack_top) = child_rip;      /* RIP: resume point setelah syscall */
    int k;
    for (k = 0; k < 15; k++) {
        /* Salin GPR induk; hanya rax (k=0) di-override jadi 0 (fork return di anak) */
        uint64_t gpr = (k == 0) ? 0ULL
                                 : *(uint64_t *)(parent_kstack_top - 48 - (uint64_t)k * 8);
        *(--stack_top) = gpr;
    }

    tasks[id].rsp = (uint64_t)stack_top;
    vfs_copy_fds(parent_tid, id);   /* child warisi semua fd dari induk (fork semantics) */
    return id;
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

/* Ekstensi B: interval timer — dipanggil dari timer_handler setiap 1ms. */
void task_itimer_tick(void) {
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].used || !tasks[i].is_user) continue;
        if (tasks[i].itimer_interval == 0) continue;
        if (tasks[i].itimer_remaining > 0) {
            tasks[i].itimer_remaining--;
            if (tasks[i].itimer_remaining == 0) {
                /* kirim SIGALRM dan reset */
                task_send_signal(i, SIGALRM);
                tasks[i].itimer_remaining = tasks[i].itimer_interval;
            }
        }
    }
}

/* Set/clear interval timer untuk task tid. */
void task_set_itimer(int tid, uint32_t interval_ms) {
    if (tid < 0 || tid >= MAX_TASKS || !tasks[tid].used) return;
    tasks[tid].itimer_interval  = interval_ms;
    tasks[tid].itimer_remaining = interval_ms;
}