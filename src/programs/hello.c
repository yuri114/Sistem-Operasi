#define SYS_PRINT 0
#define SYS_EXIT 2

static void sys_print(const char *msg) {
    __asm__ volatile (
        "mov $0, %%eax\n"
        "mov %0, %%ebx\n"
        "int $0x80\n"
        :: "r"(msg) : "eax", "ebx"
    );
}

void _start() {
    sys_print("Hello from ELF program!\n");
    // SYS_EXIT
    __asm__ volatile ("mov $2, %%eax; int $0x80" ::: "eax");
}