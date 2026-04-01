/* spinlock.h — Spinlock primitives menggunakan GCC built-in atomics
 *
 * LOCK XCHG (via __sync_lock_test_and_set) menjamin atomicity
 * dan implicit full memory barrier.
 * PAUSE hint di busy-wait mengurangi CPU pipeline stalls.
 */
#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef volatile uint32_t spinlock_t;

#define SPINLOCK_INIT  0

/* Ambil lock: spin sampai berhasil mengubah 0 → 1 secara atomik. */
static inline void spinlock_acquire(spinlock_t *lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        /* Spin: tunggu lock jadi 0 sambil yield pipeline. */
        while (*lock)
            __asm__ volatile("pause");
    }
}

/* Lepas lock: tulis 0 dengan release semantics (full compiler/HW barrier). */
static inline void spinlock_release(spinlock_t *lock) {
    __sync_lock_release(lock);
}

/* Cek apakah lock sedang dipegang (untuk debug). */
static inline int spinlock_is_locked(const spinlock_t *lock) {
    return *lock != 0;
}

#endif /* SPINLOCK_H */
