#ifndef TASK_H
#define TASK_H
#include <stdint.h>

#define MAX_TASKS  4
#define STACK_SIZE 4096

typedef struct{
    uint32_t esp; //stack pointer
    uint8_t used; //flag untuk menandai apakah slot ini digunakan
} Task;

void task_init();
int task_create(void (*entry)()); //fungsi untuk membuat task baru, mengembalikan id task atau -1 jika gagal
void task_switch(); //fungsi untuk switch ke task berikutnya
void task_set_main(); //fungsi untuk menandai task utama (shell) sudah ada, sehingga tidak perlu dibuat lagis
#endif
