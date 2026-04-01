/* condvar.h — Condition Variable (F-P4) */
#ifndef CONDVAR_H
#define CONDVAR_H
#include <stdint.h>

#define CV_MAX         8   /* jumlah condvar maksimal */
#define CV_WAITER_MAX  4   /* antrian waiter per condvar */

typedef struct {
    uint8_t used;
    int     waiters[CV_WAITER_MAX];  /* ring FIFO task yang sedang wait */
    uint8_t whead, wtail;
} CondVar;

void cv_init_all();
int  cv_alloc();
void cv_free(int id);
int  cv_wait(int id, int sem_id);       /* lepas sem, block, reacquire */
int  cv_signal(int id);                 /* bangunkan 1 waiter */
int  cv_broadcast(int id);              /* bangunkan semua waiter */

#endif
