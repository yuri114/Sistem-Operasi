#include "syscall.h"
#include "syscall_internal.h"
#include "task.h"
#include "vmm.h"
#include "elf_loader.h"
#include "fs.h"
#include "vfs.h"

uint64_t syscall_dispatch_proc(uint64_t eax, uint64_t ebx, uint64_t edx, int *handled) {
    *handled = 1;

    // SYS_KILL(9): ebx = id proses yang akan dimatikan
    if (eax == SYS_KILL) {
        return (uint32_t)task_kill((int)ebx);
    }

    // SYS_EXEC(30)
    if (eax == SYS_EXEC) {
        if (!is_user_ptr(ebx)) return (uint32_t)-1;
        const char *name = (const char*)ebx;
        uint32_t sz = 0;
        const uint8_t *data = fs_read_bin(name, &sz);
        if (!data || sz == 0) return (uint64_t)-1;
        uint64_t *proc_dir = vmm_create_page_dir();
        uint64_t entry = elf_load(data, sz, proc_dir);
        if (!entry) return (uint64_t)-1;
        uint64_t stack_phys = pmm_alloc_frame();
        vmm_map_page(proc_dir, MM_USER_STACK_PAGE, stack_phys, 7);
        int tid = task_create_user(entry, proc_dir, MM_USER_ENTRY_RSP, name, 0, 0);
        return (uint64_t)tid;
    }

    // SYS_BRK(62): perluas user heap; ebx=new_end → return new_end aktual
    if (eax == SYS_BRK) {
        int tid = task_get_current();
        uint64_t cur_end = task_get_heap_end(tid);
        uint64_t new_end = ebx;
        if (new_end == 0) return cur_end;          /* query current brk */
        if (new_end <= cur_end) return cur_end;    /* no shrink */
        if (new_end > MM_BRK_LIMIT) return cur_end; /* jangan masuk guard page */
        /* Tier-1: tegakkan rlimit_mem (KB) jika diset via SYS_SETRLIMIT. */
        {
            uint32_t mem_kb = 0; uint16_t fds_unused = 0;
            task_get_rlimit(tid, &mem_kb, &fds_unused);
            if (mem_kb && (new_end - MM_USER_HEAP_START) > (uint64_t)mem_kb * 1024ULL)
                return cur_end;
        }
        /* Petakan frame baru untuk halaman yang belum dipetakan */
        uint64_t *pdir = task_get_page_dir(tid);
        uint64_t pg;
        for (pg = (cur_end & ~(uint64_t)0xFFF); pg < new_end; pg += 0x1000) {
            uint64_t frame = pmm_alloc_frame();
            if (!frame) break;
            vmm_map_page(pdir, pg, frame, 7);  /* P+RW+User */
            cur_end = pg + 0x1000;
        }
        task_set_heap_end(tid, cur_end);
        return cur_end;
    }

    // SYS_WAITPID(63): tunggu task; ebx=tid → return exit code
    if (eax == SYS_WAITPID) {
        int wtid = (int)ebx;
        task_wait(wtid);
        return (uint64_t)(int64_t)task_get_exit_code(wtid);
    }

    // SYS_THREAD_CREATE(64): buat thread; ebx=entry_va, edx=arg → return tid
    if (eax == SYS_THREAD_CREATE) {
        int parent = task_get_current();
        int tid = task_create_thread(ebx, edx, parent);
        return (uint64_t)(int64_t)tid;
    }

    // SYS_THREAD_EXIT(65): keluar dari thread saat ini
    if (eax == SYS_THREAD_EXIT) {
        task_exit();
        return 0; /* tidak dicapai */
    }

    // SYS_THREAD_JOIN(66): tunggu thread selesai; ebx=tid
    if (eax == SYS_THREAD_JOIN) {
        task_wait((int)ebx);
        return 0;
    }

    // SYS_THREAD_SET_NAME(67): set nama thread; ebx=tid, edx=name_ptr
    if (eax == SYS_THREAD_SET_NAME) {
        if (!is_user_ptr(edx)) return (uint64_t)-1;
        task_set_name((int)ebx, (const char *)edx);
        return 0;
    }

    // ---------------------------------------------------------------
    // Fondasi Q — Proses & Memori Lanjutan
    // ---------------------------------------------------------------

    // SYS_FORK(73): fork() via int 0x80.
    // Kernel membaca RIP dan RSP user langsung dari iretq frame di kernel stack induk.
    // Induk mendapat tid anak; anak melanjutkan eksekusi dari RIP user dengan rax=0.
    if (eax == SYS_FORK) {
        int parent = task_get_current();
        uint64_t *parent_pml4 = task_get_page_dir(parent);
        if (!parent_pml4) return (uint64_t)-1;

        /* Baca RIP dan RSP user dari frame iretq di kernel stack induk.
         * Layout kernel stack saat int80_handler + call syscall_handler:
         *   kstack_top-8   = SS user
         *   kstack_top-16  = RSP user   ← kita ambil
         *   kstack_top-24  = RFLAGS
         *   kstack_top-32  = CS user
         *   kstack_top-40  = RIP user   ← kita ambil (= titik resume setelah int $0x80)
         */
        uint64_t kstack_top = task_get_rsp0(parent);
        uint64_t child_rip  = *(uint64_t *)(kstack_top - 40);
        uint64_t child_rsp  = *(uint64_t *)(kstack_top - 16);

        if (!is_user_ptr(child_rip) || !is_user_ptr(child_rsp))
            return (uint64_t)-1;

        /* Buat PML4 anak */
        uint64_t *child_pml4 = vmm_create_page_dir();
        if (!child_pml4) return (uint64_t)-1;

        /* Setup COW antara parent dan child */
        vmm_copy_cow(parent_pml4, child_pml4);

        /* Buat task anak */
        int child = task_create_fork(parent, child_rip, child_rsp);
        if (child < 0) { vmm_free_user_memory(child_pml4); return (uint64_t)-1; }

        /* Pasang PML4 anak */
        task_set_page_dir(child, child_pml4);

        return (uint64_t)(int64_t)child;  /* induk mendapat tid anak */
    }

    // SYS_EXEC_REPLACE(74): ganti image proses; ebx=ptr nama file
    // Tidak pernah kembali ke pemanggil — langsung iretq ke entry baru.
    if (eax == SYS_EXEC_REPLACE) {
        if (!is_user_ptr(ebx)) return (uint64_t)-1;
        const char *name = (const char *)ebx;
        uint32_t sz = 0;
        const uint8_t *data = fs_read_bin(name, &sz);
        if (!data || sz == 0) return (uint64_t)-1;

        /* Muat ELF ke PML4 baru */
        uint64_t *new_dir = vmm_create_page_dir();
        if (!new_dir) return (uint64_t)-1;
        uint64_t entry = elf_load(data, sz, new_dir);
        if (!entry) { vmm_free_user_memory(new_dir); return (uint64_t)-1; }

        uint64_t stack_frame = pmm_alloc_frame();
        if (!stack_frame) { vmm_free_user_memory(new_dir); return (uint64_t)-1; }
        vmm_map_page(new_dir, MM_USER_STACK_PAGE, stack_frame, 7);

        int tid = task_get_current();
        uint64_t *old_dir = task_get_page_dir(tid);

        /* Reset state task */
        task_set_page_dir(tid, new_dir);
        task_set_heap_end(tid, MM_USER_HEAP_START);
        vfs_close_all(tid);
        vfs_init_task(tid);

        /* Switch ke PML4 baru dan bebaskan PML4 lama */
        vmm_switch_dir(new_dir);
        vmm_free_user_memory(old_dir);

        /* Lompat langsung ke user space lewat iretq — tidak kembali dari syscall */
        __asm__ volatile (
            "cli\n\t"
            "push $0x23\n\t"        /* SS: user data  */
            "push %1\n\t"           /* RSP: user stack */
            "push $0x202\n\t"       /* RFLAGS: IF=1   */
            "push $0x2B\n\t"        /* CS: user code  */
            "push %0\n\t"           /* RIP: entry     */
            "iretq\n\t"
            :: "r"(entry), "r"((uint64_t)MM_USER_ENTRY_RSP)
            : "memory"
        );
        __builtin_unreachable();
    }

    // SYS_MMAP(75): alloc N halaman anonim; ebx=n_pages → return VA awal, 0=gagal
    if (eax == SYS_MMAP) {
        uint64_t n = ebx;
        if (n == 0 || n > 256) return 0;   /* batasi 1MB per mmap */
        int tid = task_get_current();
        uint64_t *pdir = task_get_page_dir(tid);
        if (!pdir) return 0;
        /* VA bump allocator mulai dari 0x900000, batas atas 0xB00000 (region mmap_file) */
        static uint64_t mmap_next_va = MM_MMAP_ANON_BASE;
        if (mmap_next_va + n * 0x1000ULL > MM_MMAP_ANON_LIMIT) return 0;
        uint64_t va_base = mmap_next_va;
        uint64_t i;
        for (i = 0; i < n; i++) {
            uint64_t frame = pmm_alloc_frame();
            if (!frame) {
                /* rollback: unmap yang sudah dipetakan */
                uint64_t j;
                for (j = 0; j < i; j++) {
                    uint64_t va = va_base + j * 0x1000ULL;
                    uint64_t phys = vmm_get_phys(pdir, va);
                    vmm_unmap_page(pdir, va);
                    if (phys >= 768ULL * 4096) pmm_free_frame(phys);
                }
                return 0;
            }
            /* zero-fill frame */
            uint8_t *fp = (uint8_t *)frame;
            uint32_t b;
            for (b = 0; b < 4096; b++) fp[b] = 0;
            vmm_map_page(pdir, va_base + i * 0x1000ULL, frame, 7);
        }
        mmap_next_va += n * 0x1000ULL;
        return va_base;
    }

    // SYS_MUNMAP(76): bebaskan mapping; ebx=va, edx=n_pages
    if (eax == SYS_MUNMAP) {
        uint64_t va   = ebx & ~(uint64_t)0xFFF;
        uint64_t n    = edx;
        if (n == 0 || va < MM_MMAP_ANON_BASE) return 0;
        int tid = task_get_current();
        uint64_t *pdir = task_get_page_dir(tid);
        if (!pdir) return 0;
        uint64_t i;
        for (i = 0; i < n; i++) {
            uint64_t page_va = va + i * 0x1000ULL;
            uint64_t phys = vmm_get_phys(pdir, page_va);
            vmm_unmap_page(pdir, page_va);
            if (phys >= 768ULL * 4096) pmm_free_frame(phys);
        }
        return 0;
    }

    // ---------------------------------------------------------------
    // Fondasi AX — file-backed mmap / munmap
    // ---------------------------------------------------------------

    // SYS_MMAP_FILE(98): mmap file dari fd.
    // ebx=fd, edx=n_pages (0=auto dari ukuran file) → VA awal, 0=gagal
    if (eax == SYS_MMAP_FILE) {
        int fd = (int)ebx;
        int tid = task_get_current();
        uint64_t *pdir = task_get_page_dir(tid);
        if (!pdir) return 0;

        /* Seek ke awal, lalu baca sampai EOF */
        vfs_seek(tid, fd, 0);

        static uint64_t mmap_file_next_va = MM_MMAP_FILE_BASE;  /* terpisah dari anonim */
        uint64_t va_base = mmap_file_next_va;

        /* Baca file 4096 byte per halaman; stop saat vfs_read < 4096 */
        uint64_t max_pages = (uint64_t)edx;
        if (max_pages == 0) max_pages = 256;  /* auto: max 1MB */
        if (max_pages > 256) max_pages = 256;

        uint64_t npages = 0;
        uint64_t i;
        for (i = 0; i < max_pages; i++) {
            uint64_t frame = pmm_alloc_frame();
            if (!frame) break;
            uint8_t *fp = (uint8_t *)frame;
            /* zero-fill */
            uint32_t b; for (b = 0; b < 4096; b++) fp[b] = 0;
            /* Baca satu halaman dari file */
            int nr = vfs_read(tid, fd, (char *)fp, 4096);
            vmm_map_page(pdir, va_base + i * 0x1000ULL, frame, 7); /* P+RW+User */
            npages++;
            if (nr < 4096) break;  /* EOF */
        }
        if (npages == 0) return 0;
        mmap_file_next_va += (npages + 1) * 0x1000ULL;  /* +1 guard page */
        return va_base;
    }

    // SYS_MUNMAP_FILE(99): bebaskan file-backed mapping; sama seperti MUNMAP
    if (eax == SYS_MUNMAP_FILE) {
        uint64_t va = ebx & ~(uint64_t)0xFFF;
        uint64_t n  = edx;
        if (n == 0) return 0;
        int tid = task_get_current();
        uint64_t *pdir = task_get_page_dir(tid);
        if (!pdir) return 0;
        uint64_t i;
        for (i = 0; i < n; i++) {
            uint64_t page_va = va + i * 0x1000ULL;
            uint64_t phys = vmm_get_phys(pdir, page_va);
            vmm_unmap_page(pdir, page_va);
            if (phys >= 768ULL * 4096) pmm_free_frame(phys);
        }
        return 0;
    }

    // SYS_SIGACTION(87): daftarkan handler sinyal (stub — hanya terima, belum dipakai)
    if (eax == SYS_SIGACTION) {
        return 0;  /* stub: handler belum didelivery ke user space */
    }

    // SYS_SIGKILL_SIG(88): kirim sinyal ke task: ebx=tid, edx=sig
    if (eax == SYS_SIGKILL_SIG) {
        task_send_signal((int)ebx, (int)edx);
        return 0;
    }

    // SYS_GET_TLS(91): kembalikan VA halaman TLS task saat ini
    if (eax == SYS_GET_TLS) {
        int cur = task_get_current();
        return MM_TLS_BASE + (uint64_t)(uint32_t)cur * MM_TLS_PAGE_SIZE;
    }

    /* Fondasi AA — SYS_GETARGV(95): kembalikan argc, isi buf + argv[] pointers.
     * rdi = char *buf       (user buffer, menerima null-terminated strings "arg0\0arg1\0...")
     * rsi = char **argv_out (user pointer array, akan diisi &buf[offset_k] per arg)
     * return = argc */
    if (eax == SYS_GETARGV) {
        char *ubuf     = (char *)(uintptr_t)ebx;
        char **uargv   = (char **)(uintptr_t)edx;
        if (!is_user_ptr(ebx) || !is_user_ptr(edx)) return 0;
        int tid = task_get_current();
        int ac  = tasks_getargc(tid);
        /* Defensif: uargv di user-space diasumsikan punya 9 slot (argv[0..7] + NULL).
         * ac sudah dibatasi <=8 di task.c, tapi clamp di sini agar penulisan
         * uargv[ac] selalu di dalam batas walau batas itu berubah nanti. */
        if (ac > 8) ac = 8;
        const char *src = tasks_getargbuf(tid);  /* pointer ke kernel arg_buf */
        /* Salin arg_buf ke user buffer (max 512 byte) */
        int i;
        for (i = 0; i < 512; i++) {
            ubuf[i] = src[i];
            /* Dua null berturut = akhir serial */
            if (i > 0 && src[i-1] == '\0' && src[i] == '\0') { i++; break; }
        }
        ubuf[511] = '\0';
        /* Bangun pointer array — arahkan ke substring di ubuf */
        char *p = ubuf;
        for (i = 0; i < ac; i++) {
            uargv[i] = p;
            while (*p) p++;
            p++;  /* lewati null */
        }
        uargv[ac] = 0;  /* null sentinel */
        return (uint64_t)(unsigned int)ac;
    }

    // SYS_SETITIMER(104): set interval timer: ebx=interval_ms (0=off)
    if (eax == SYS_SETITIMER) {
        int tid = task_get_current();
        uint32_t interval = (uint32_t)ebx;
        task_set_itimer(tid, interval);
        return 0;
    }

    // SYS_SIGPROCMASK(107): ebx=how (SIG_BLOCK/UNBLOCK/SETMASK), edx=mask → old_mask
    if (eax == SYS_SIGPROCMASK) {
        int tid = task_get_current();
        uint32_t old  = task_get_signal_mask(tid);
        uint32_t mask = (uint32_t)edx;
        int how = (int)ebx;
        if (how == SIG_BLOCK)        task_set_signal_mask(tid, old | mask);
        else if (how == SIG_UNBLOCK) task_set_signal_mask(tid, old & ~mask);
        else                         task_set_signal_mask(tid, mask);  /* SIG_SETMASK */
        return (uint64_t)old;
    }

    // SYS_SETRLIMIT(108): ebx=ptr{uint32_t mem_kb; uint16_t fds} → 0
    if (eax == SYS_SETRLIMIT) {
        if (!ebx || !is_user_ptr(ebx)) return (uint64_t)-1;
        typedef struct { uint32_t mem_kb; uint16_t fds; } RLArgs;
        RLArgs *a = (RLArgs *)(uintptr_t)ebx;
        task_set_rlimit(task_get_current(), a->mem_kb, a->fds);
        return 0;
    }

    // SYS_GETRLIMIT(109): ebx=ptr{uint32_t mem_kb; uint16_t fds} (output) → 0
    if (eax == SYS_GETRLIMIT) {
        if (!ebx || !is_user_ptr(ebx)) return (uint64_t)-1;
        typedef struct { uint32_t mem_kb; uint16_t fds; } RLArgs;
        RLArgs *a = (RLArgs *)(uintptr_t)ebx;
        task_get_rlimit(task_get_current(), &a->mem_kb, &a->fds);
        return 0;
    }

    *handled = 0;
    return 0;
}
