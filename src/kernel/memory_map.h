#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

/* ---- Physical memory layout ---- */
#define MM_HEAP_START         0x100000ULL   /* kernel heap start (memory.c HEAP_START) */
#define MM_HEAP_SIZE          0x600000ULL   /* kernel heap size  (memory.c HEAP_SIZE) */
#define MM_KERNEL_IMAGE_BASE  0x700000ULL   /* kernel .text/.data base, linker.ld */
#define MM_BSS_BASE           0x4000000ULL  /* kernel .bss base, linker.ld + vmm.c pd_low[32] */
#define MM_KERNEL_RESERVED_FRAMES 2048      /* 8MB / 4KB, vmm.c KERNEL_RESERVED_FRAMES */

/* ---- User virtual address layout ---- */
#define MM_USER_CODE_BASE     0x300000ULL   /* user ELF load base; is_user_ptr() floor */
#define MM_USER_HEAP_START    0x400000ULL   /* user heap / brk start; demand-page floor */
#define MM_USER_STACK_PAGE    0x600000ULL   /* single-page user stack (exec/fork) */
#define MM_USER_ENTRY_RSP     0x601000ULL   /* initial RSP = stack page + 0x1000 */
#define MM_STACK_GUARD_LO     0x5FE000ULL   /* [lo, hi) = stack-overflow guard page */
#define MM_STACK_GUARD_HI     0x5FF000ULL
#define MM_BRK_LIMIT          0x5FE000ULL   /* SYS_BRK tidak boleh masuk guard page */

/* ---- Thread stacks and TLS ---- */
#define MM_THREAD_STACK_BASE  0x700000ULL   /* per-thread 16KB slot region; NOTE: nilai
                                                sama dgn MM_KERNEL_IMAGE_BASE tapi address
                                                space/arti beda (user VA vs kernel-image
                                                VA) - sengaja 2 konstanta terpisah */
#define MM_THREAD_STACK_SLOT  0x5000ULL     /* ukuran slot per-thread (5 x 4KB) */
#define MM_THREAD_STACK_GUARD_PAGES 1        /* page 0 tiap slot = guard, unmapped */
#define MM_TLS_BASE           0x800000ULL   /* TLS region base, 1 page per task */
#define MM_TLS_PAGE_SIZE      0x1000ULL

/* ---- mmap regions ---- */
#define MM_MMAP_ANON_BASE     0x900000ULL   /* SYS_MMAP bump allocator start */
#define MM_MMAP_ANON_LIMIT    0xB00000ULL   /* SYS_MMAP upper bound */
#define MM_MMAP_FILE_BASE     0xB00000ULL   /* SYS_MMAP_FILE bump allocator start */

/* ---- demand-paging user VA range (kernel.c exception_handler) ---- */
#define MM_USER_VA_LOW        0x400000ULL
#define MM_USER_VA_HIGH       0x80000000ULL

#endif
