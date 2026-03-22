#ifndef TASK_H
#define TASK_H
#include <stdint.h>

#define MAX_TASKS  4
#define STACK_SIZE 4096

typedef struct{
    uint32_t esp; //stack pointer
    uint8_t used; //flag untuk menandai apakah slot ini digunakan
    uint32_t *page_dir; //pointer ke page directory untuk task ini
} Task;

void task_init();
int task_create(void (*entry)()); //fungsi untuk membuat task baru, mengembalikan id task atau -1 jika gagal
int task_create_user(uint32_t entry, uint32_t *page_dir, uint32_t user_esp); //buat task ring 3 dengan page directory dan user stack sendiri
void task_switch(); //fungsi untuk switch ke task berikutnya
void task_set_main(); //fungsi untuk menandai task utama (shell) sudah ada, sehingga tidak perlu dibuat lagis
void task_exit(); //fungsi untuk keluar dari task saat ini, akan dipanggil dari syscall SYS_EXIT

#endif
