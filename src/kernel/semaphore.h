#ifndef SEMAPHORE_H
#define SEMAPHORE_H
#include <stdint.h>

// Semaphore kernel — maksimum 16 semaphore global
#define SEM_MAX        16
#define SEM_WAITER_MAX  4   /* maks waiter per semaphore */

typedef struct {
    int8_t  value;                     // >= 0: tersedia
    uint8_t used;                      // slot aktif?
    int     waiters[SEM_WAITER_MAX];   // FIFO: tid yang sedang tblokir
    uint8_t whead, wtail;              // head / tail ring
} Semaphore;

void sem_init_all();                    // inisialisasi semua slot
int  sem_alloc(int initial_value);      // alokasi semaphore baru, return id atau -1
void sem_free(int id);                  // bebaskan slot
int  sem_wait(int id);                  // decrement; block jika 0
int  sem_post(int id);                  // increment; bangunkan waiter

#endif
