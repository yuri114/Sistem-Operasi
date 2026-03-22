#include "task.h"
#include "vmm.h"
#include "tss.h"

static void str_copy_n(char *dst, const char *src, int n) {
    int i;
    for (i = 0; i < n - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static Task tasks[MAX_TASKS]; //array untuk menyimpan task
static uint8_t stacks[MAX_TASKS][STACK_SIZE]; //stack untuk setiap task
static int current_task = 0; //id task yang sedang berjalan
static int task_count = 0; //jumlah task yang telah dibuat
uint32_t *current_esp; //pointer untuk menyimpan esp task saat ini
uint32_t *next_esp; //pointer untuk menyimpan esp task berikutnya

void task_init() {
    int i;
    for (i = 0; i < MAX_TASKS; i++){
        tasks[i].used = 0;
    }
    
}

void task_set_main() {
    task_count = 1;
    tasks[0].used = 1;
    str_copy_n(tasks[0].name, "[shell]", 32);
}

 int task_create(void (*entry)()){
    if (task_count >= MAX_TASKS){
        return -1;
    }

    int id = task_count++;
    tasks[id].used = 1;
    str_copy_n(tasks[id].name, "[idle]", 32);
    tasks[id].page_dir = vmm_create_page_dir();
    //Inisialisasi stack untuk task baru
    uint32_t *stack_top = (uint32_t*)(stacks[id] + STACK_SIZE); //mulai dari atas stack

    // Urutan: push eflags, cs, eip (seperti stack saat interrupt)
    // lalu pusha (8 register = 8x uint32_t)
    *(--stack_top) = 0x202; //eflags dengan interrupt enable
    *(--stack_top) = 0x08; //cs (kode segmen kernel)
    *(--stack_top) = (uint32_t)entry; //eip (alamat fungsi entry point)
    //pusha register umum (eax, ecx, edx, ebx, esp, ebp, esi, edi)
    *(--stack_top) = 0; //eax
    *(--stack_top) = 0; //ecx
    *(--stack_top) = 0; //edx
    *(--stack_top) = 0; //ebx
    *(--stack_top) = 0; //esp
    *(--stack_top) = 0; //ebp
    *(--stack_top) = 0; //esi
    *(--stack_top) = 0; //edi
    *(--stack_top) = 0x10; //ds (data segment, ini yang di-pop oleh "pop eax" di irq0)

    tasks[id].esp = (uint32_t)stack_top; //simpan stack pointer awal untuk task ini
    return id; //kembalikan id task yang baru dibuat
 }

 void task_switch() {
    if (task_count < 2 ) return; //tidak ada task lain untuk switch

    int next = (current_task + 1) % task_count; //pilih task berikutnya secara round-robin

    //skip task yang tidak digunakan (seharusnya tidak terjadi karena task_count sudah dihitung)
    int i;
    for (i = 0; i < task_count; i++)
    {
        if (tasks[next].used) {
            break;
        }
        next = (next + 1) % task_count;
    }
    if (!tasks[next].used)return; //tidak ada task lain yang aktif
    if (next == current_task) return; //hanya ada satu task yang aktif

    //Simpan konteks task saat ini (esp)
    extern uint32_t *current_esp;
    extern uint32_t *next_esp;
    current_esp = &tasks[current_task].esp; //simpan esp task saat ini
    next_esp = &tasks[next].esp; //siapkan esp untuk task berikutnya
    current_task = next; //update task yang sedang berjalan

    // update TSS agar interrupt saat ring 3 menggunakan kernel stack task yang benar
    tss_set_kernel_stack((uint32_t)(stacks[next] + STACK_SIZE));

    // switch ke page directory task berikutnya (kalau ada)
    extern void vmm_switch_dir(uint32_t*);
    if (tasks[current_task].page_dir) {
        vmm_switch_dir(tasks[current_task].page_dir);
    }
 }
 
int task_create_user(uint32_t entry, uint32_t *page_dir, uint32_t user_esp, const char *name) {
    if (task_count >= MAX_TASKS) return -1;

    int id = task_count++;
    tasks[id].used = 1;
    tasks[id].page_dir = page_dir;
    str_copy_n(tasks[id].name, name ? name : "?", 32);

    uint32_t *stack_top = (uint32_t*)(stacks[id] + STACK_SIZE);

    // iret frame untuk transisi ke ring 3:
    // CPU pop eip, cs, eflags, esp_user, ss saat iret ke ring <= privilege level rendah
    *(--stack_top) = 0x23;       // ss  (user data segment, ring 3)
    *(--stack_top) = user_esp;   // esp (user stack pointer)
    *(--stack_top) = 0x202;      // eflags (IF=1, agar interrupt aktif)
    *(--stack_top) = 0x1B;       // cs  (user code segment, ring 3)
    *(--stack_top) = entry;      // eip (alamat entry point ELF)
    // pusha: register umum (di-restore dengan popa di irq0)
    *(--stack_top) = 0; // eax
    *(--stack_top) = 0; // ecx
    *(--stack_top) = 0; // edx
    *(--stack_top) = 0; // ebx
    *(--stack_top) = 0; // esp (dummy, tidak dipakai)
    *(--stack_top) = 0; // ebp
    *(--stack_top) = 0; // esi
    *(--stack_top) = 0; // edi
    *(--stack_top) = 0x23; // ds  (user data segment, di-pop pertama oleh irq0)

    tasks[id].esp = (uint32_t)stack_top;
    return id;
}

 void task_exit() {
    tasks[current_task].used = 0;
    __asm__ volatile ("sti");
    while (1) {
        __asm__ volatile ("hlt");
    }
 }

// --- fungsi info untuk ps command ---
int task_get_max()         { return MAX_TASKS; }
int task_is_used(int id)   { return (id >= 0 && id < MAX_TASKS) ? tasks[id].used : 0; }
const char *task_get_name(int id) { return (id >= 0 && id < MAX_TASKS) ? tasks[id].name : ""; }
int task_get_current()     { return current_task; }

// kill: matikan task dengan id tertentu
// tidak boleh kill id 0 (shell)
int task_kill(int id) {
    if (id <= 0 || id >= MAX_TASKS) return 0;  // id 0 = shell, lindungi
    if (!tasks[id].used) return 0;             // sudah mati
    tasks[id].used = 0;
    // jika task_count menunjuk slot ini, kurangi agar slot bisa dipakai ulang
    if (id == task_count - 1) task_count--;
    return 1;
}