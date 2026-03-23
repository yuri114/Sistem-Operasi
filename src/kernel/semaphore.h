#ifndef SEMAPHORE_H
#define SEMAPHORE_H
#include <stdint.h>

// Semaphore kernel — maksimum 8 semaphore global
#define SEM_MAX 8

typedef struct {
    int8_t  value;  // >= 0: tersedia, < 0: ada waiter
    uint8_t used;   // slot aktif?
} Semaphore;

void sem_init_all();                    // inisialisasi semua slot
int  sem_alloc(int initial_value);      // alokasi semaphore baru, return id (0-7) atau -1
void sem_free(int id);                  // bebaskan slot
int  sem_wait(int id);                  // decrement; block (busy-wait) jika 0
int  sem_post(int id);                  // increment; release

#endif
