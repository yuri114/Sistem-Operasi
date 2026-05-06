/* serial.c — Driver Serial COM1 (0x3F8) untuk debug output
 * QEMU: tambahkan -serial stdio pada command line untuk membaca output ini.
 * Fondasi AS: Kernel Debugger via serial — command: kdbg
 */
#include <stdint.h>
#include "serial.h"

#define COM1_BASE   0x3F8

#define COM1_DATA   (COM1_BASE + 0)   /* Data register           */
#define COM1_IER    (COM1_BASE + 1)   /* Interrupt Enable        */
#define COM1_FCR    (COM1_BASE + 2)   /* FIFO Control Register   */
#define COM1_LCR    (COM1_BASE + 3)   /* Line Control Register   */
#define COM1_MCR    (COM1_BASE + 4)   /* Modem Control Register  */
#define COM1_LSR    (COM1_BASE + 5)   /* Line Status Register    */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* Inisialisasi COM1: 9600 baud, 8N1, no IRQ. */
void serial_init() {
    outb(COM1_IER, 0x00);   /* Matikan semua interrupt            */
    outb(COM1_LCR, 0x80);   /* Enable DLAB (set baud rate divisor)*/
    outb(COM1_DATA, 0x0C);  /* Divisor low byte: 12 → 9600 baud  */
    outb(COM1_IER, 0x00);   /* Divisor high byte: 0               */
    outb(COM1_LCR, 0x03);   /* 8-bit, no parity, 1 stop bit       */
    outb(COM1_FCR, 0xC7);   /* FIFO on, clear, threshold 14 byte  */
    outb(COM1_MCR, 0x0B);   /* IRQs on, RTS/DSR on                */
}

/* Tunggu sampai transmit buffer kosong lalu kirim satu byte. */
void serial_putchar(char c) {
    int timeout = 0x10000;
    while (!(inb(COM1_LSR) & 0x20) && timeout--);
    if (c == '\n') {
        /* Kirim CR+LF agar terbaca lebih jelas di terminal host */
        while (!(inb(COM1_LSR) & 0x20));
        outb(COM1_DATA, '\r');
    }
    while (!(inb(COM1_LSR) & 0x20));
    outb(COM1_DATA, (uint8_t)c);
}

/* Kirim string ke COM1. */
void serial_print(const char *s) {
    while (*s) serial_putchar(*s++);
}

/* Kirim nilai hex 64-bit ke COM1. */
void serial_print_hex(unsigned long long v) {
    const char *hex = "0123456789ABCDEF";
    int i;
    serial_print("0x");
    for (i = 60; i >= 0; i -= 4)
        serial_putchar(hex[(v >> i) & 0xF]);
}

/* ================================================================
 * Fondasi AZ — dmesg ring buffer
 * ================================================================ */
static char  dmesg_buf[DMESG_BUF_SIZE];
static int   dmesg_head = 0;  /* posisi tulis berikutnya */
static int   dmesg_len  = 0;  /* jumlah byte valid (maks DMESG_BUF_SIZE-1) */

void dmesg_log(const char *s) {
    while (*s) {
        dmesg_buf[dmesg_head] = *s++;
        dmesg_head = (dmesg_head + 1) % DMESG_BUF_SIZE;
        if (dmesg_len < DMESG_BUF_SIZE - 1) dmesg_len++;
    }
}

int dmesg_read(char *buf, int maxlen) {
    if (maxlen <= 0) return 0;
    int to_copy = dmesg_len;
    if (to_copy >= maxlen) to_copy = maxlen - 1;
    /* start = head - len (wrapped) */
    int start = (dmesg_head - dmesg_len + DMESG_BUF_SIZE * 2) % DMESG_BUF_SIZE;
    int i;
    for (i = 0; i < to_copy; i++)
        buf[i] = dmesg_buf[(start + i) % DMESG_BUF_SIZE];
    buf[to_copy] = '\0';
    return to_copy;
}

/* ================================================================
 * Fondasi AS — Kernel Debugger via Serial (COM1)
 * ================================================================ */

/* Return 1 jika ada karakter tersedia di COM1. */
int serial_haschar(void) {
    return (inb(COM1_LSR) & 0x01) != 0;
}

/* Terima satu karakter dari COM1 (blocking). */
char serial_getchar(void) {
    while (!(inb(COM1_LSR) & 0x01));
    return (char)inb(COM1_DATA);
}

/* ------------------------------------------------------------------ */
/* Mini helpers untuk kdbg                                            */
/* ------------------------------------------------------------------ */
static void kdbg_puts(const char *s) { serial_print(s); }
static void kdbg_putc(char c) { serial_putchar(c); }

static void kdbg_print_hex8(uint8_t v) {
    const char *h="0123456789ABCDEF";
    serial_putchar(h[v>>4]); serial_putchar(h[v&0xF]);
}
static void kdbg_print_hex64(uint64_t v) {
    int i;
    serial_print("0x");
    for (i=60; i>=0; i-=4)
        serial_putchar("0123456789ABCDEF"[(v>>i)&0xF]);
}
static void kdbg_print_uint(uint64_t n) {
    char buf[21]; int i=0;
    if(n==0){serial_putchar('0');return;}
    while(n){buf[i++]=(char)('0'+n%10);n/=10;}
    while(--i>=0) serial_putchar(buf[i]);
}

/* Baca satu baris dari serial (max 127 karakter). Return panjang. */
static int kdbg_readline(char *buf, int maxlen) {
    int i=0;
    while (i < maxlen-1) {
        char c = serial_getchar();
        if (c == '\r' || c == '\n') {
            serial_print("\r\n");
            break;
        } else if (c == '\b' || c == 127) {
            if (i>0) { i--; serial_print("\b \b"); }
        } else if (c >= 0x20 && c < 0x7F) {
            buf[i++] = c;
            serial_putchar(c);
        }
    }
    buf[i] = 0;
    return i;
}

/* Parse hex number from string, advance *p. Return 0 if no digits. */
static uint64_t kdbg_parse_hex(const char **p) {
    uint64_t v=0; int got=0;
    while(**p) {
        char c=**p;
        if(c>='0'&&c<='9'){v=(v<<4)|(uint64_t)(c-'0');got=1;(*p)++;}
        else if(c>='a'&&c<='f'){v=(v<<4)|(uint64_t)(c-'a'+10);got=1;(*p)++;}
        else if(c>='A'&&c<='F'){v=(v<<4)|(uint64_t)(c-'A'+10);got=1;(*p)++;}
        else break;
    }
    (void)got;
    return v;
}

/* Parse decimal number. */
static uint64_t kdbg_parse_dec(const char **p) {
    uint64_t v=0;
    while(**p>='0'&&**p<='9'){v=v*10+(uint64_t)(**p-'0');(*p)++;}
    return v;
}

/* Hex dump memory: addr, n bytes */
static void kdbg_hexdump(uint64_t addr, uint32_t n) {
    uint32_t i;
    for (i=0; i<n; i++) {
        if ((i%16)==0) {
            kdbg_print_hex64(addr+i);
            kdbg_puts(": ");
        }
        /* Read byte carefully — no MMU guard in this educational OS */
        uint8_t b = *((volatile uint8_t *)(uintptr_t)(addr+i));
        kdbg_print_hex8(b);
        kdbg_putc(' ');
        if ((i%16)==15 || i==n-1) {
            /* Pad if short line */
            if (i==n-1 && (i%16)!=15) {
                uint32_t pad=(15-(i%16))*3; uint32_t pp;
                for(pp=0;pp<pad;pp++) kdbg_putc(' ');
            }
            /* ASCII */
            kdbg_puts(" |");
            uint32_t row_start = i - (i%16);
            uint32_t j;
            for(j=row_start; j<=i; j++) {
                uint8_t cb = *((volatile uint8_t *)(uintptr_t)(addr+j));
                kdbg_putc(cb>=0x20&&cb<0x7F ? (char)cb : '.');
            }
            kdbg_puts("|\r\n");
        }
    }
}

/* Backtrace via RBP chain */
static void kdbg_backtrace(void) {
    uint64_t rbp;
    __asm__ volatile ("mov %%rbp, %0" : "=r"(rbp));
    int depth=0;
    kdbg_puts("Backtrace (RBP chain):\r\n");
    while (rbp != 0 && depth < 16) {
        /* Frame: [rbp+0]=prev_rbp, [rbp+8]=return_addr */
        uint64_t ret_addr = *((volatile uint64_t *)(uintptr_t)(rbp+8));
        uint64_t prev_rbp = *((volatile uint64_t *)(uintptr_t)(rbp));
        kdbg_puts("  #");
        kdbg_print_uint((uint64_t)depth);
        kdbg_puts("  rip="); kdbg_print_hex64(ret_addr);
        kdbg_puts("  rbp="); kdbg_print_hex64(rbp);
        kdbg_puts("\r\n");
        depth++;
        if (prev_rbp <= rbp || prev_rbp == 0) break; /* avoid infinite loop */
        rbp = prev_rbp;
    }
}

/* Dump registers (call from kdbg so registers reflect kdbg_run frame) */
static void kdbg_regs(void) {
    uint64_t rax,rbx,rcx,rdx,rsi,rdi,rbp,rsp,r8,r9,r10,r11,r12,r13,r14,r15;
    __asm__ volatile (
        "mov %%rax,%0\n mov %%rbx,%1\n mov %%rcx,%2\n mov %%rdx,%3\n"
        "mov %%rsi,%4\n mov %%rdi,%5\n mov %%rbp,%6\n mov %%rsp,%7\n"
        "mov %%r8,%8\n  mov %%r9,%9\n  mov %%r10,%10\n mov %%r11,%11\n"
        "mov %%r12,%12\n mov %%r13,%13\n mov %%r14,%14\n mov %%r15,%15\n"
        : "=m"(rax),"=m"(rbx),"=m"(rcx),"=m"(rdx),
          "=m"(rsi),"=m"(rdi),"=m"(rbp),"=m"(rsp),
          "=m"(r8), "=m"(r9), "=m"(r10),"=m"(r11),
          "=m"(r12),"=m"(r13),"=m"(r14),"=m"(r15)
    );
    kdbg_puts("  RAX="); kdbg_print_hex64(rax); kdbg_puts("  RBX="); kdbg_print_hex64(rbx); kdbg_puts("\r\n");
    kdbg_puts("  RCX="); kdbg_print_hex64(rcx); kdbg_puts("  RDX="); kdbg_print_hex64(rdx); kdbg_puts("\r\n");
    kdbg_puts("  RSI="); kdbg_print_hex64(rsi); kdbg_puts("  RDI="); kdbg_print_hex64(rdi); kdbg_puts("\r\n");
    kdbg_puts("  RBP="); kdbg_print_hex64(rbp); kdbg_puts("  RSP="); kdbg_print_hex64(rsp); kdbg_puts("\r\n");
    kdbg_puts("  R8 ="); kdbg_print_hex64(r8);  kdbg_puts("  R9 ="); kdbg_print_hex64(r9);  kdbg_puts("\r\n");
    kdbg_puts("  R10="); kdbg_print_hex64(r10); kdbg_puts("  R11="); kdbg_print_hex64(r11); kdbg_puts("\r\n");
    kdbg_puts("  R12="); kdbg_print_hex64(r12); kdbg_puts("  R13="); kdbg_print_hex64(r13); kdbg_puts("\r\n");
    kdbg_puts("  R14="); kdbg_print_hex64(r14); kdbg_puts("  R15="); kdbg_print_hex64(r15); kdbg_puts("\r\n");
}

/* ------------------------------------------------------------------ */
/* kdbg_run — entry point debugger                                    */
/* ------------------------------------------------------------------ */
void kdbg_run(void) {
    char line[128];
    kdbg_puts("\r\n=== Oria Kernel Debugger (COM1) ===\r\n");
    kdbg_puts("Perintah: x <addr> [n]  bt  r  help  q\r\n");
    kdbg_puts("Contoh:   x 0x100000 64   (hex dump 64 byte)\r\n\r\n");
    for (;;) {
        kdbg_puts("kdbg> ");
        kdbg_readline(line, 128);
        const char *p = line;
        while (*p == ' ') p++;
        if (*p == 0) continue;

        if (p[0]=='q' && p[1]==0) {
            kdbg_puts("kdbg: keluar.\r\n");
            break;
        } else if (p[0]=='h' && p[1]=='e' && p[2]=='l') {
            kdbg_puts("  x <addr_hex> [n_dec]  — hex dump n byte dari addr (default 64)\r\n");
            kdbg_puts("  bt                    — backtrace via RBP chain\r\n");
            kdbg_puts("  r                     — dump register\r\n");
            kdbg_puts("  q                     — keluar dari debugger\r\n");
        } else if (p[0]=='b' && p[1]=='t') {
            kdbg_backtrace();
        } else if (p[0]=='r' && p[1]==0) {
            kdbg_regs();
        } else if (p[0]=='x' && (p[1]==' '||p[1]==0)) {
            p++; while(*p==' ') p++;
            uint64_t addr = 0;
            if (p[0]=='0' && p[1]=='x') { p+=2; addr=kdbg_parse_hex(&p); }
            else addr=kdbg_parse_hex(&p);
            while(*p==' ') p++;
            uint32_t n = 64;
            if (*p>='0'&&*p<='9') n=(uint32_t)kdbg_parse_dec(&p);
            if (n==0) n=64;
            if (n>4096) n=4096;
            kdbg_hexdump(addr, n);
        } else {
            kdbg_puts("kdbg: perintah tidak dikenal. Ketik 'help'.\r\n");
        }
    }
}
