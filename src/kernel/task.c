#include "task.h"

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
    task_count = 1; //anggap sudah ada 1 task (main)
    tasks[0].used = 1; //tandai slot ini digunakan
}

 int task_create(void (*entry)()){
    if (task_count >= MAX_TASKS){
        return -1; //gagal, sudah mencapai batas maksimal task
    }

    int id = task_count++; //gunakan id berikutnya
    tasks[id].used = 1; //tandai slot ini digunakan
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

    //Simpan konteks task saat ini (esp)
    extern uint32_t *current_esp;
    extern uint32_t *next_esp;
    current_esp = &tasks[current_task].esp; //simpan esp task saat ini
    next_esp = &tasks[next].esp; //siapkan esp untuk task berikutnya
    current_task = next; //update task yang sedang berjalan
 }