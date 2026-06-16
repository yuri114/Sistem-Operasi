#include "syscall.h"
#include "syscall_internal.h"
#include "task.h"
#include "fs.h"
#include "vmm.h"
#include "elf_loader.h"

uint64_t syscall_handler(uint64_t eax, uint64_t ebx, uint64_t edx) {
    /* F-T: Cek sinyal pending sebelum memproses syscall apapun.
     * Jika ada SIGKILL/SIGTERM/SIGINT, terminasi task ini sekarang. */
    task_check_signals();

    int handled = 0;
    uint64_t r;
    r = syscall_dispatch_core(eax, ebx, edx, &handled); if (handled) return r;
    r = syscall_dispatch_fs  (eax, ebx, edx, &handled); if (handled) return r;
    r = syscall_dispatch_ipc (eax, ebx, edx, &handled); if (handled) return r;
    r = syscall_dispatch_gfx (eax, ebx, edx, &handled); if (handled) return r;
    r = syscall_dispatch_proc(eax, ebx, edx, &handled); if (handled) return r;
    r = syscall_dispatch_net (eax, ebx, edx, &handled); if (handled) return r;
    return (uint64_t)-1; //kembalikan -1 untuk menandakan syscall tidak dikenal
}

/* Jalankan program dari FS langsung dari kernel (dipakai taskbar quick-launch) */
int kernel_exec(const char *name) {
    uint32_t sz = 0;
    const uint8_t *data = fs_read_bin(name, &sz);
    if (!data || sz == 0) return -1;
    uint64_t *proc_dir = vmm_create_page_dir();
    uint64_t entry = elf_load(data, sz, proc_dir);
    if (!entry) return -1;
    uint64_t stack_phys = pmm_alloc_frame();
    vmm_map_page(proc_dir, MM_USER_STACK_PAGE, stack_phys, 7);
    return task_create_user(entry, proc_dir, MM_USER_ENTRY_RSP, name, 0, 0);
}
