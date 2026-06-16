#ifndef SYSCALL_INTERNAL_H
#define SYSCALL_INTERNAL_H
#include <stdint.h>
#include "memory_map.h"

/* Validasi pointer dari user space: tolak NULL dan pointer ke kernel space (<0x300000).
 * User programs dimuat di 0x300000+. Stack user di 0x400000+. */
static inline int is_user_ptr(uint64_t ptr) {
    return (ptr != 0 && ptr >= MM_USER_CODE_BASE);
}

/* Dispatcher per-subsistem syscall — dipanggil dari syscall_handler() di syscall.c.
 * Tiap dispatcher set *handled=1 dan kembalikan hasil jika eax cocok salah satu
 * cabang grup ini, atau set *handled=0 (return value diabaikan) jika tidak. */
uint64_t syscall_dispatch_core(uint64_t eax, uint64_t ebx, uint64_t edx, int *handled);
uint64_t syscall_dispatch_fs  (uint64_t eax, uint64_t ebx, uint64_t edx, int *handled);
uint64_t syscall_dispatch_ipc (uint64_t eax, uint64_t ebx, uint64_t edx, int *handled);
uint64_t syscall_dispatch_gfx (uint64_t eax, uint64_t ebx, uint64_t edx, int *handled);
uint64_t syscall_dispatch_proc(uint64_t eax, uint64_t ebx, uint64_t edx, int *handled);
uint64_t syscall_dispatch_net (uint64_t eax, uint64_t ebx, uint64_t edx, int *handled);

#endif
