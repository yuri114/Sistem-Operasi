#ifndef SEMAPHORE_H
#define SEMAPHORE_H
#include <stdint.h>

// Semaphore kernel — maksimum 16 semaphore global
#define SEM_MAX 16

typedef struct {
    int8_t  value;   // >= 0: tersedia, == 0 saat ada waiter
    uint8_t used;    // slot aktif?
    int     waiter;  // tid task yang sedang diblokir di sem_wait (-1 = tidak ada)
} Semaphore;

void sem_init_all();                    // inisialisasi semua slot
int  sem_alloc(int initial_value);      // alokasi semaphore baru, return id (0-7) atau -1
void sem_free(int id);                  // bebaskan slot
int  sem_wait(int id);                  // decrement; block (busy-wait) jika 0
int  sem_post(int id);                  // increment; release

#endif
