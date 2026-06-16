/*kernel.c - kernel utama*/
#include "idt.h"
#include "pic.h"
#include "shell.h"
#include "memory.h"
#include "memory_map.h"
#include "timer.h"
#include "fs.h"
#include "ata.h"
#include "mouse.h"
#include "window.h"
#include "paging.h"
#include "task.h"
#include "syscall.h"
#include "tss.h"
#include "vmm.h"
#include "elf_loader.h"
#include "hello_elf_data.h"
#include "sender_elf_data.h"
#include "writer_elf_data.h"
#include "piper_elf_data.h"
#include "pipe_sender_elf_data.h"
#include "pipe_receiver_elf_data.h"
#include "devtest_elf_data.h"
#include "gfxtest_elf_data.h"
#include "gui_demo_elf_data.h"
#include "gui_term_elf_data.h"
#include "paint_elf_data.h"
#include "calc_elf_data.h"
#include "notepad_elf_data.h"
#include "filemanager_elf_data.h"
#include "clock_elf_data.h"
#include "sysinfo_elf_data.h"
#include "threadtest_elf_data.h"
#include "condtest_elf_data.h"
#include "forktest_elf_data.h"
#include "pipetest_elf_data.h"
#include "grep_elf_data.h"
#include "sort_elf_data.h"
#include "ls_elf_data.h"
#include "sigtest_elf_data.h"
#include "futextest_elf_data.h"
#include "mfs4test_elf_data.h"
#include "polltest_elf_data.h"
#include "cat_elf_data.h"
#include "wc_elf_data.h"
#include "head_elf_data.h"
#include "tail_elf_data.h"
#include "less_elf_data.h"
#include "cp_elf_data.h"
#include "mv_elf_data.h"
#include "edit_elf_data.h"
#include "ipc.h"
#include "semaphore.h"
#include "pipe.h"
#include "shm.h"
#include "device.h"
#include "drv_vga.h"
#include "vfs.h"
#include "mq.h"
#include "drv_kbd.h"
#include "graphics.h"
#include "vbe.h"
#include "keyboard.h"
#include "serial.h"
#include "net.h"
#include "virtio_blk.h"
#include "smp.h"
#include "acpi.h"
#include "condvar.h"
#include "mfs4.h"

/* VBE 1920×1080 @ 32bpp: font 8×16 = 160 kolom × 60 baris */
#define VGA_COLS 160
#define VGA_ROWS 60

/* ================================================================
 * Fondasi AV — Virtual Terminals (tty0–tty5)
 * Setiap VT menyimpan text buffer, fg buffer, dan posisi kursor.
 * ================================================================ */
#define VT_COUNT 6

typedef struct {
    char     textbuf[VGA_ROWS][VGA_COLS];
    uint32_t fgbuf  [VGA_ROWS][VGA_COLS];
    int      cursor_col;
    int      cursor_row;
    int      input_start_col;
    int      input_start_row;
    uint32_t fg_color;
    uint32_t bg_color;
} VtCtx;

static VtCtx vt_ctx[VT_COUNT];
int g_active_vt = 0;

/* Inisialisasi semua VT dengan warna default */
static void vt_init_all(void) {
    int i, r, c;
    for (i = 0; i < VT_COUNT; i++) {
        for (r = 0; r < VGA_ROWS; r++)
            for (c = 0; c < VGA_COLS; c++) {
                vt_ctx[i].textbuf[r][c] = 0;
                vt_ctx[i].fgbuf[r][c]   = GFX_LGRAY;
            }
        vt_ctx[i].cursor_col       = 0;
        vt_ctx[i].cursor_row       = 0;
        vt_ctx[i].input_start_col  = 0;
        vt_ctx[i].input_start_row  = 0;
        vt_ctx[i].fg_color         = GFX_LGRAY;
        vt_ctx[i].bg_color         = GFX_BLACK;
    }
}

/* Forward declaration */
void vt_switch(int n);

/* posisi kursor teks saat ini (dalam satuan sel karakter 8x8) */
int cursor_col = 0;
int cursor_row = 0;
int input_start_row = 0;
int input_start_col = 0;

/* Mode OS: 0=console, 1=GUI */
int g_gui_mode = 0;
void scroll();
void update_cursor();
void itoa(uint32_t num, char *buf);

/* Warna teks saat ini (32bpp True Color) */
uint8_t current_color = 0x0f;  /* legacy compat drv_vga */
uint32_t fg_color = GFX_LGRAY;  /* warna foreground karakter */
uint32_t bg_color = GFX_BLACK;  /* warna background sel */

/* ---- text cell buffer (untuk redraw di atas wallpaper) ---- */
static char     g_textbuf[VGA_ROWS][VGA_COLS];
static uint32_t g_fgbuf  [VGA_ROWS][VGA_COLS];
void vga_put_char_at(int col, int row, char c, uint32_t color);

void set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

/* Forward declaration for ANSI parser */
void clear_screen(void);

/* ---------------------------------------------------------------
 * Fondasi AG — ANSI escape code parser
 * Mendukung: ESC[<n>m (SGR), ESC[2J (clear), ESC[row;colH (cursor),
 *            ESC[nA/B/C/D (cursor move), ESC[K (clear to EOL)
 * --------------------------------------------------------------- */
static int   ansi_state = 0;         /* 0=normal 1=ESC 2=bracket */
static char  ansi_buf[24];
static int   ansi_len  = 0;

/* Tabel 8 warna standar ANSI (0..7) — dark + bright */
static const uint32_t ansi_dark[8] = {
    0x00000000u, 0x00AA0000u, 0x0000AA00u, 0x00AA5500u,
    0x000000AAu, 0x00AA00AAu, 0x0000AAAAu, 0x00AAAAAAu,
};
static const uint32_t ansi_bright[8] = {
    0x00555555u, 0x00FF5555u, 0x0055FF55u, 0x00FFFF55u,
    0x005555FFu, 0x00FF55FFu, 0x0055FFFFu, 0x00FFFFFFu,
};

/* Helper: parse semicolon-sep numbers dari ansi_buf → params[], return count */
static int ansi_parse_params(int *params, int maxp) {
    int np = 0, i = 0, v = 0, has = 0;
    while (ansi_buf[i] || has) {
        char ch = ansi_buf[i];
        if (ch >= '0' && ch <= '9') { v = v*10 + (ch-'0'); has = 1; i++; }
        else { if (np < maxp) params[np++] = v; v = 0; has = 0;
               if (ch == ';') i++; else break; }
    }
    return np;
}

static void ansi_dispatch(char term) {
    int params[8]; int np = ansi_parse_params(params, 8);
    int n0 = (np > 0) ? params[0] : 0;
    int n1 = (np > 1) ? params[1] : 0;

    if (term == 'm') {  /* SGR — color/attribute */
        if (np == 0) { fg_color = 0x00FFFFFFu; bg_color = 0x00000000u; return; }
        int p;
        for (p = 0; p < np; p++) {
            int n = params[p];
            if (n == 0) { fg_color = 0x00FFFFFFu; bg_color = 0x00000000u; }
            else if (n == 1) {
                int ci;
                for (ci = 0; ci < 8; ci++)
                    if (fg_color == ansi_dark[ci]) { fg_color = ansi_bright[ci]; break; }
            }
            else if (n >= 30 && n <= 37) fg_color = ansi_dark [n-30];
            else if (n == 39)            fg_color = 0x00FFFFFFu;
            else if (n >= 40 && n <= 47) bg_color = ansi_dark [n-40];
            else if (n == 49)            bg_color = 0x00000000u;
            else if (n >= 90 && n <= 97) fg_color = ansi_bright[n-90];
            else if (n >= 100&& n <=107) bg_color = ansi_bright[n-100];
        }
    } else if (term == 'H' || term == 'f') {  /* cursor position ESC[row;colH */
        int r = (n0 > 0) ? n0 - 1 : 0;
        int c = (n1 > 0) ? n1 - 1 : 0;
        if (r < 0) r = 0; if (r >= VGA_ROWS) r = VGA_ROWS - 1;
        if (c < 0) c = 0; if (c >= VGA_COLS) c = VGA_COLS - 1;
        cursor_row = r; cursor_col = c;
        update_cursor();
    } else if (term == 'A') {  /* cursor up */
        int n = (n0 > 0) ? n0 : 1;
        cursor_row -= n; if (cursor_row < 0) cursor_row = 0;
        update_cursor();
    } else if (term == 'B') {  /* cursor down */
        int n = (n0 > 0) ? n0 : 1;
        cursor_row += n; if (cursor_row >= VGA_ROWS) cursor_row = VGA_ROWS-1;
        update_cursor();
    } else if (term == 'C') {  /* cursor right */
        int n = (n0 > 0) ? n0 : 1;
        cursor_col += n; if (cursor_col >= VGA_COLS) cursor_col = VGA_COLS-1;
        update_cursor();
    } else if (term == 'D') {  /* cursor left */
        int n = (n0 > 0) ? n0 : 1;
        cursor_col -= n; if (cursor_col < 0) cursor_col = 0;
        update_cursor();
    } else if (term == 'J') {  /* clear screen / part of screen */
        if (n0 == 2) { clear_screen(); }  /* ESC[2J = clear all */
        /* ESC[0J or ESC[J = clear from cursor to end — simplified: just clear screen */
    } else if (term == 'K') {  /* erase line */
        int col;
        if (n0 == 0) {  /* clear cursor to end of line */
            for (col = cursor_col; col < VGA_COLS; col++) {
                g_textbuf[cursor_row][col] = 0;
                draw_char_gfx16(col*8, cursor_row*16, ' ', fg_color, bg_color);
            }
        } else if (n0 == 1) {  /* clear start of line to cursor */
            for (col = 0; col <= cursor_col; col++) {
                g_textbuf[cursor_row][col] = 0;
                draw_char_gfx16(col*8, cursor_row*16, ' ', fg_color, bg_color);
            }
        } else if (n0 == 2) {  /* clear whole line */
            for (col = 0; col < VGA_COLS; col++) {
                g_textbuf[cursor_row][col] = 0;
                draw_char_gfx16(col*8, cursor_row*16, ' ', fg_color, bg_color);
            }
        }
    }
}

/* Redraw semua karakter dari g_textbuf di atas bg solid */
static void terminal_redraw(void) {
    int r, c;
    fill_screen(bg_color);
    for (r = 0; r < VGA_ROWS; r++)
        for (c = 0; c < VGA_COLS; c++)
            if (g_textbuf[r][c])
                draw_char_gfx16(c * 8, r * 16, g_textbuf[r][c],
                                g_fgbuf[r][c], bg_color);
}

/* ================================================================
 * Fondasi AV — vt_switch: pindah ke terminal virtual nomor n
 * Simpan state aktif ke vt_ctx[g_active_vt], restore vt_ctx[n].
 * ================================================================ */
void vt_switch(int n) {
    int r, c;
    if (n < 0 || n >= VT_COUNT || n == g_active_vt || g_gui_mode) return;

    /* Simpan state VT aktif */
    for (r = 0; r < VGA_ROWS; r++)
        for (c = 0; c < VGA_COLS; c++) {
            vt_ctx[g_active_vt].textbuf[r][c] = g_textbuf[r][c];
            vt_ctx[g_active_vt].fgbuf[r][c]   = g_fgbuf[r][c];
        }
    vt_ctx[g_active_vt].cursor_col      = cursor_col;
    vt_ctx[g_active_vt].cursor_row      = cursor_row;
    vt_ctx[g_active_vt].input_start_col = input_start_col;
    vt_ctx[g_active_vt].input_start_row = input_start_row;
    vt_ctx[g_active_vt].fg_color        = fg_color;
    vt_ctx[g_active_vt].bg_color        = bg_color;

    /* Restore VT n */
    g_active_vt = n;
    for (r = 0; r < VGA_ROWS; r++)
        for (c = 0; c < VGA_COLS; c++) {
            g_textbuf[r][c] = vt_ctx[n].textbuf[r][c];
            g_fgbuf[r][c]   = vt_ctx[n].fgbuf[r][c];
        }
    cursor_col      = vt_ctx[n].cursor_col;
    cursor_row      = vt_ctx[n].cursor_row;
    input_start_col = vt_ctx[n].input_start_col;
    input_start_row = vt_ctx[n].input_start_row;
    fg_color        = vt_ctx[n].fg_color;
    bg_color        = vt_ctx[n].bg_color;

    /* Redraw dari buffer VT baru */
    terminal_redraw();
}

/*fungsi: hapus seluruh layar*/
void clear_screen() {
    int r, c;
    for (r = 0; r < VGA_ROWS; r++)
        for (c = 0; c < VGA_COLS; c++) {
            g_textbuf[r][c] = 0;
            g_fgbuf  [r][c] = fg_color;
        }
    fill_screen(bg_color);
    cursor_col = 0;
    cursor_row = 0;
}

/*fungsi: cetak suatu karakter ke layar*/
void print_char(char c) {
    /* --- ANSI state machine --- */
    if (ansi_state == 1) {
        if (c == '[') { ansi_state = 2; ansi_len = 0; return; }
        ansi_state = 0;   /* fallthrough untuk cetak karakter biasa */
    }
    if (ansi_state == 2) {
        /* Terminal characters for ANSI sequences */
        if (c == 'm' || c == 'J' || c == 'H' || c == 'f' ||
            c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'K') {
            ansi_buf[ansi_len] = '\0';
            ansi_dispatch(c);
            ansi_state = 0;
        } else if (ansi_len < 23) {
            ansi_buf[ansi_len++] = c;
        }
        return;
    }
    if (c == '\033') { ansi_state = 1; return; }
    /* --- end ANSI --- */
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= VGA_ROWS) scroll();
        return;
    }
    g_textbuf[cursor_row][cursor_col] = c;
    g_fgbuf  [cursor_row][cursor_col] = fg_color;
    draw_char_gfx16(cursor_col * 8, cursor_row * 16, c, fg_color, bg_color);
    cursor_col++;
    if (cursor_col >= VGA_COLS) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= VGA_ROWS) scroll();
    }
    update_cursor();
}

void backspace_char() {
    if (cursor_row == input_start_row && cursor_col == input_start_col) return;
    if (cursor_col == 0) {
        cursor_row--;
        cursor_col = VGA_COLS - 1;
    } else {
        cursor_col--;
    }
    g_textbuf[cursor_row][cursor_col] = 0;
    g_fgbuf  [cursor_row][cursor_col] = fg_color;
    draw_char_gfx16(cursor_col * 8, cursor_row * 16, ' ', fg_color, bg_color);
    update_cursor();
}

void scroll() {
    /* Geser text buffer satu baris ke atas */
    int r, c;
    for (r = 0; r < VGA_ROWS - 1; r++)
        for (c = 0; c < VGA_COLS; c++) {
            g_textbuf[r][c] = g_textbuf[r + 1][c];
            g_fgbuf  [r][c] = g_fgbuf  [r + 1][c];
        }
    for (c = 0; c < VGA_COLS; c++) {
        g_textbuf[VGA_ROWS - 1][c] = 0;
        g_fgbuf  [VGA_ROWS - 1][c] = fg_color;
    }
    terminal_redraw();
    cursor_row = VGA_ROWS - 1;
}

static inline void outb(uint16_t port, uint8_t value){
    __asm__ volatile ("outb %0, %1":: "a"(value), "Nd"(port));
}

void update_cursor() {
    /* Di Mode 13h hardware cursor tidak terlihat — no-op.
     * Software cursor (opsional) dapat ditambahkan di fase mendatang. */
}

/*fungsi: cetak string ke layar*/
void print(const char *str){
    int i=0;
    while (str[i] !='\0')//loop sampai akhir string (null terminator)
    {
        print_char(str[i]);//cetak karakter saat ini
        i++;
    }
}

void vga_put_char_at(int col, int row, char c, uint32_t color) {
    draw_char_gfx(col * 8, row * 8, c, color, bg_color);
}

void my_background_task() {
    while(1) {
        /* background task — tidak menggambar langsung ke framebuffer
         * agar tidak menyebabkan flicker */
        uint32_t i;
        for (i = 0; i < 5000000; i++) {
            __asm__ volatile ("nop");
        }
    }
}

void enter_usermode(uint64_t rip, uint64_t user_rsp) {
    __asm__ volatile (
        "mov $0x23, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushq $0x23\n"         /* SS: user data */
        "pushq %1\n"            /* RSP: user mode stack */
        "pushfq\n"              /* RFLAGS */
        "pop %%rax\n"
        "or $0x200, %%rax\n"    /* set IF */
        "push %%rax\n"
        "pushq $0x2B\n"         /* CS: user code (GDT 0x28 | RPL=3) */
        "pushq %0\n"            /* RIP */
        "iretq\n"
        :: "r"(rip), "r"(user_rsp) : "rax"
    );
}

void itoa(uint32_t num, char *buf) {
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    int i = 0;
    char temp[32]; //buffer sementara untuk menyimpan digit dalam urutan terbalik

    while (num > 0)
    {
        temp[i++] = '0' + (num % 10); //ambil digit terakhir dan simpan sebagai karakter
        num /= 10; //hapus digit terakhir
    }
    int j;
    for (j = 0; j < i; j++)
    {
        buf[j] = temp[i - j - 1]; //balik urutan digit
    }
    buf[i] = '\0'; //tutup string dengan null terminator

}

void user_task() {

    /*
    // user mode task - kosong untuk sementara
    // test SYS_PRINT
    const char *msg = "[user] tekan sembarang tombol:";
    __asm__ volatile ("mov $0, %%eax; mov %0, %%ebx; int $0x80":: "r"(msg) : "eax", "ebx");

    //test SYS_GETKEY - tunggu 1 tombol simpan hasilnya di EAX
    uint32_t key;
    __asm__ volatile ("mov $1, %%eax; int $0x80; mov %%eax, %0" : "=r"(key) : : "eax");
    
    //test SYS_PRINT lagi untuk menampilkan karakter yang ditekan
    const char *msg2 = "\n[user] tombol diterima! keluar... \n";
    __asm__ volatile ("mov $0, %%eax; mov %0, %%ebx; int $0x80":: "r"(msg2) : "eax", "ebx");
    
    //test SYS_EXIT untuk keluar dari task ini
    __asm__ volatile ("mov $2, %%eax; int $0x80":: : "eax");
    */
   while (1){}
   
}

void programs_init() {
    fs_write_bin("hello",         build_hello_elf,         build_hello_elf_len);
    fs_write_bin("sender",        build_sender_elf,        build_sender_elf_len);
    fs_write_bin("writer",        build_writer_elf,        build_writer_elf_len);
    fs_write_bin("piper",         build_piper_elf,         build_piper_elf_len);
    fs_write_bin("pipe_sender",   build_pipe_sender_elf,   build_pipe_sender_elf_len);
    fs_write_bin("pipe_receiver", build_pipe_receiver_elf, build_pipe_receiver_elf_len);
    fs_write_bin("devtest",       build_devtest_elf,       build_devtest_elf_len);
    fs_write_bin("gfxtest",       build_gfxtest_elf,       build_gfxtest_elf_len);
    fs_write_bin("gui_demo",      build_gui_demo_elf,      build_gui_demo_elf_len);
    fs_write_bin("gui_term",      build_gui_term_elf,      build_gui_term_elf_len);
    fs_write_bin("paint",         build_paint_elf,         build_paint_elf_len);
    fs_write_bin("calc",          build_calc_elf,          build_calc_elf_len);
    fs_write_bin("notepad",       build_notepad_elf,       build_notepad_elf_len);
    fs_write_bin("filemanager",   build_filemanager_elf,   build_filemanager_elf_len);
    fs_write_bin("clock",         build_clock_elf,         build_clock_elf_len);
    fs_write_bin("sysinfo",       build_sysinfo_elf,       build_sysinfo_elf_len);
    fs_write_bin("threadtest",    build_threadtest_elf,    build_threadtest_elf_len);
    fs_write_bin("condtest",      build_condtest_elf,      build_condtest_elf_len);
    fs_write_bin("forktest",      build_forktest_elf,      build_forktest_elf_len);
    fs_write_bin("pipetest",      build_pipetest_elf,      build_pipetest_elf_len);
    fs_write_bin("grep",          build_grep_elf,          build_grep_elf_len);
    fs_write_bin("sort",          build_sort_elf,          build_sort_elf_len);
    fs_write_bin("ls",            build_ls_elf,            build_ls_elf_len);
    fs_write_bin("sigtest",       build_sigtest_elf,       build_sigtest_elf_len);
    fs_write_bin("futextest",     build_futextest_elf,     build_futextest_elf_len);
    fs_write_bin("mfs4test",      build_mfs4test_elf,      build_mfs4test_elf_len);
    fs_write_bin("polltest",      build_polltest_elf,      build_polltest_elf_len);
    /* Fondasi AB — tools */
    fs_write_bin("cat",           build_cat_elf,           build_cat_elf_len);
    fs_write_bin("wc",            build_wc_elf,            build_wc_elf_len);
    fs_write_bin("head",          build_head_elf,          build_head_elf_len);
    fs_write_bin("tail",          build_tail_elf,          build_tail_elf_len);
    fs_write_bin("less",          build_less_elf,          build_less_elf_len);
    fs_write_bin("more",          build_less_elf,          build_less_elf_len);
    fs_write_bin("cp",            build_cp_elf,            build_cp_elf_len);
    fs_write_bin("mv",            build_mv_elf,            build_mv_elf_len);
    fs_write_bin("edit",          build_edit_elf,          build_edit_elf_len);
}

/* Deklarasi handler dari isr.asm */
extern void irq0();
extern void irq1();
extern void int80_handler();
extern void syscall_entry();    /* D1: SYSCALL/SYSRET handler */
extern void exc0();  extern void exc1();  extern void exc2();
extern void exc3();  extern void exc4();  extern void exc5();
extern void exc6();  extern void exc7();  extern void exc8();
extern void exc9();  extern void exc10(); extern void exc11();
extern void exc12(); extern void exc13(); extern void exc14();
/* Cetak nilai 64-bit dalam format hex 16 digit */
static void print_hex64(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    char buf[17];
    int i;
    for (i = 0; i < 16; i++) {
        buf[15 - i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
    print(buf);
}

/* ---- F-Q1: Copy-on-Write fault (present + write + user) ----
 * Return 1 jika ditangani sebagai COW (caller harus return, retry instruksi),
 * 0 jika bukan COW (jatuh ke #PF panic/demand-paging). */
static int handle_cow_fault(uint64_t exc_num, uint64_t error_code, uint64_t cr2) {
    if (exc_num == 14 && (error_code & 7u) == 7u) {
        /* error_code bit0=present, bit1=write, bit2=user — semua set */
        uint64_t cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
        uint64_t *pml4 = (uint64_t *)(cr3 & ~(uint64_t)0xFFF);
        if (vmm_cow_fault(pml4, cr2)) return 1;  /* COW ditangani, retry instruksi */
        /* Bukan COW — jatuh ke #PF panic di bawah */
    }
    return 0;
}

/* Tampilkan panic layar-merah "stack overflow" untuk task saat ini, lalu
 * matikan task tersebut dan halt selamanya. Dipakai untuk guard page stack
 * user maupun guard page stack thread (F-P1) — beda hanya judul pesan. */
static void panic_stack_overflow(uint64_t cr2, const char *label) {
    int _tid = task_get_current();
    char _nb[8];
    fill_screen(GFX_RED);
    cursor_row = 0; cursor_col = 0;
    set_color(GFX_WHITE, GFX_RED);
    print("== "); print(label); print(" ==\n");
    print("Task ID : "); itoa((uint32_t)_tid, _nb); print(_nb); print("\n");
    print("CR2     : 0x"); print_hex64(cr2); print("\n");
    print("Task killed.\n");
    task_exit();
    for (;;) __asm__ volatile("hlt");
}

/* ---- D3: Demand paging ----
 * Return 1 jika di-demand-page (caller harus return, retry instruksi),
 * 0 jika harus jatuh ke #PF panic. */
static int handle_demand_paging(uint64_t exc_num, uint64_t error_code, uint64_t cr2) {
    if (exc_num != 14) return 0;

    /* Kondisi demand-page:
     *   bit 0 error_code = 0  → page not present (bukan protection violation)
     *   bit 2 error_code = 1  → akses dari user mode
     *   cr2 dalam range user space: 0x400000 – 0x7FFFFFFF */
    if (!(error_code & 1u) && (error_code & 4u)
        && cr2 >= MM_USER_VA_LOW && cr2 < MM_USER_VA_HIGH)
    {
        /* Tahap K: Guard page — CR2 di [0x5FE000, 0x5FEFFF] = stack overflow */
        if (cr2 >= MM_STACK_GUARD_LO && cr2 < MM_STACK_GUARD_HI) {
            panic_stack_overflow(cr2, "STACK OVERFLOW");
        }
        /* F-P1: Guard page stack thread — slot di VA 0x700000 + id*0x5000 */
        if (cr2 >= MM_THREAD_STACK_BASE && cr2 < MM_TLS_BASE) {
            uint64_t off = cr2 - MM_THREAD_STACK_BASE;
            if ((off % MM_THREAD_STACK_SLOT) < 0x1000ULL) {
                panic_stack_overflow(cr2, "THREAD STACK OVERFLOW");
            }
        }
        /* Baca PML4 dari CR3 */
        uint64_t cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
        uint64_t *pml4 = (uint64_t *)(cr3 & ~(uint64_t)0xFFF);

        /* Alokasikan frame baru dan zeroing (keamanan: jangan bocor data) */
        uint64_t phys = pmm_alloc_frame();
        if (phys) {
            vmm_zero_frame(phys);
            /* Petakan halaman yang di-fault (page-aligned) */
            vmm_map_page(pml4, cr2 & ~(uint64_t)0xFFF, phys, 7);
            return 1;  /* kembali: retry instruksi */
        }
        /* Tidak cukup memori — jatuh ke panic */
    }
    return 0;
}

/* Tampilkan layar Kernel Panic merah dengan detail exception, lalu halt
 * selamanya. Tidak pernah return. */
static void show_kernel_panic(uint64_t exc_num, uint64_t error_code,
                               uint64_t rip, uint64_t cr2) {
    static const char *names[] = {
        "#DE Divide Error",         "#DB Debug",
        "NMI",                      "#BP Breakpoint",
        "#OF Overflow",             "#BR Bound Range",
        "#UD Invalid Opcode",       "#NM Device Not Available",
        "#DF Double Fault",         "Coprocessor Overrun",
        "#TS Invalid TSS",          "#NP Segment Not Present",
        "#SS Stack Fault",          "#GP General Protection",
        "#PF Page Fault"
    };
    char num_buf[12];

    /* Layar merah — gunakan VBE framebuffer yang sudah terpeta */
    fill_screen(GFX_RED);
    cursor_row = 0; cursor_col = 0;
    set_color(GFX_WHITE, GFX_RED);

    print("========== KERNEL PANIC ==========\n\n");
    print("Exception : ");
    print(exc_num < 15 ? names[exc_num] : "Unknown");
    print("  (INT ");
    itoa((uint32_t)exc_num, num_buf); print(num_buf);
    print(")\nErr Code  : 0x"); print_hex64(error_code);
    print("\nRIP       : 0x"); print_hex64(rip);

    if (exc_num == 14) {   /* Page Fault — CR2 berisi alamat yang menyebabkan fault */
        print("\nCR2       : 0x"); print_hex64(cr2);
        print("\nAccess    : ");
        print(error_code & 4 ? "User " : "Kernel ");
        print(error_code & 2 ? "Write "  : "Read ");
        print(error_code & 1 ? "(Protection Violation)" : "(Page Not Present)");
    }

    print("\n\nSystem Halted.\n");
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

/* Dipanggil dari exc_common di isr.asm saat CPU exception terjadi.
 * Untuk #PF dari user mode (page not present, alamat valid): demand-page.
 * Semua exception lain: tampilkan Kernel Panic dan halt. */
void exception_handler(uint64_t exc_num, uint64_t error_code,
                       uint64_t rip,      uint64_t cr2) {
    if (handle_cow_fault(exc_num, error_code, cr2)) return;
    if (handle_demand_paging(exc_num, error_code, cr2)) return;
    show_kernel_panic(exc_num, error_code, rip, cr2);
}

/*entry point kernel - dipanggil dari kernel_entry.asm*/
void kernel_main(){
    /* Inisialisasi serial COM1 sedini mungkin untuk debug output */
    serial_init();
    serial_print("[BOOT] kernel_main() entered\n");

    /* Diagnostik: tulis ke VGA text buffer (0xB8000) sebelum VBE aktif. */
    volatile uint16_t *vga_dbg = (volatile uint16_t *)0xB8000;
    vga_dbg[0] = 0x0F4B; vga_dbg[1] = 0x0F43; vga_dbg[2] = 0x0F20;

    /* Heap WAJIB siap sebelum graphics_set_fb() (malloc back buffer di
     * bawah) — jika dipanggil sebelum mem_init(), heap_head masih NULL,
     * malloc() mengembalikan 0. */
    mem_init();

    /* 1. Paging */
    paging_init();
    vga_dbg[3] = 0x0F50;

    /* 2. VBE LFB */
    uint32_t lfb_addr = vbe_find_lfb();
    vga_dbg[4] = 0x0F46;
    paging_map_vbe(lfb_addr);
    vga_dbg[5] = 0x0F4D;

    /* 3. Set mode grafis */
    vbe_set_mode(1920, 1080, 32);
    graphics_set_fb(lfb_addr);
    graphics_init();
    vt_init_all();   /* AV: inisialisasi semua virtual terminal */
    clear_screen();
    print("=================================");
    print("\n   Selamat datang di Oria OS!   \n");
    print("=================================");
    print("\nKernel berjalan di Long Mode (64-bit)\n");
    shell_init();
    pmm_init();
    ata_init();
    virtio_blk_init();
    net_init();
    fs_init();
    mfs4_init();
    ipc_init();
    sem_init_all();
    pipe_init_all();
    cv_init_all();
    shm_init();

    // Daftarkan dan inisialisasi device driver
    dev_register(DEV_VGA, &drv_vga);
    dev_register(DEV_KBD, &drv_kbd);
    dev_init_all();
    programs_init();
    timer_init(TIMER_HZ);
    pic_init();
    idt_init();
    idt_set_gate(32, (uint64_t)irq0);
    idt_set_gate(33, (uint64_t)irq1);

    /* Exception handlers INT 0-14 */
    idt_set_gate(0,  (uint64_t)exc0);
    idt_set_gate(1,  (uint64_t)exc1);
    idt_set_gate(2,  (uint64_t)exc2);
    idt_set_gate(3,  (uint64_t)exc3);
    idt_set_gate(4,  (uint64_t)exc4);
    idt_set_gate(5,  (uint64_t)exc5);
    idt_set_gate(6,  (uint64_t)exc6);
    idt_set_gate(7,  (uint64_t)exc7);
    idt_set_gate(8,  (uint64_t)exc8);
    idt_set_gate(9,  (uint64_t)exc9);
    idt_set_gate(10, (uint64_t)exc10);
    idt_set_gate(11, (uint64_t)exc11);
    idt_set_gate(12, (uint64_t)exc12);
    idt_set_gate(13, (uint64_t)exc13);
    idt_set_gate(14, (uint64_t)exc14);

    input_start_row = cursor_row;
    input_start_col = cursor_col;

    // inisialisasi multitasking
    task_init();
    task_set_main();
    /* Tahap J: inisialisasi VFS + MQ, setup fd 0/1/2 untuk shell (task 0) */
    vfs_init();
    mq_init();
    vfs_init_task(0);
    /* TSS esp0 harus menunjuk ke puncak kernel stack task 0 (shell),
     * sehingga saat ring-3 → ring-0 transition, CPU memakai stack yang benar. */
    tss64_init(task_get_rsp0(0));
    /* register syscall dengan DPL = 3 agar ring-3 bisa memanggil int 0x80 */
    idt_set_gate_user(0x80, (uint64_t)int80_handler);

    /* D1: Setup SYSCALL/SYSRET via MSR
     * STAR[47:32] = 0x0008  → SYSCALL CS=0x08, SS=0x10
     * STAR[63:48] = 0x0018  → SYSRETQ CS=(0x18+16)|3=0x2B, SS=(0x18+8)|3=0x23
     * LSTAR = address of syscall_entry
     * SFMASK = 0x200 (clear IF=bit 9 on SYSCALL) */
    {
        uint32_t lo, hi;
        /* Set SCE bit in EFER (bit 0) */
        __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080u));
        lo |= 0x1u;
        __asm__ volatile ("wrmsr" :: "c"(0xC0000080u), "a"(lo), "d"(hi));
        /* STAR MSR */
        lo = 0;
        hi = (0x0018u << 16) | 0x0008u;    /* [63:48]=0x0018, [47:32]=0x0008 */
        __asm__ volatile ("wrmsr" :: "c"(0xC0000081u), "a"(lo), "d"(hi));
        /* LSTAR MSR */
        {
            uint64_t lstar = (uint64_t)(uintptr_t)syscall_entry;
            __asm__ volatile ("wrmsr" :: "c"(0xC0000082u),
                              "a"((uint32_t)lstar), "d"((uint32_t)(lstar >> 32)));
        }
        /* SFMASK MSR: clear IF (bit 9) on SYSCALL */
        __asm__ volatile ("wrmsr" :: "c"(0xC0000084u), "a"(0x200u), "d"(0u));
    }
    serial_print("[D1] SYSCALL/SYSRET MSR configured\n");

    /* Tahap H: bootstrap SMP (LAPIC + ACPI MADT + INIT/SIPI AP). */
    smp_init();

    /* Tahap I: buat background kernel task untuk AP (work stealing). */
    if (cpu_count > 1)
        task_create(smp_background_task);

    /* IRQ12 — PS/2 Mouse (INT 44 = slave IRQ4) */
    extern void irq12();
    idt_set_gate(44, (uint64_t)irq12);
    /* wm_init() dan mouse_init() TIDAK dipanggil di sini.
     * Diaktifkan oleh command 'gui' dari shell. */

    __asm__ volatile ("sti");

    /* Shell polling loop — berjalan di ring 0, bukan di interrupt context.
     * Dulu shell_process_char dipanggil dari irq1 (keyboard interrupt), artinya
     * draw_char_gfx (64 pixel write) terjadi di dalam interrupt dengan IF=0.
     * Akibatnya: (1) delay lama antar keystroke, (2) fast typing → crash karena
     * banyak timer interrupt tertunda langsung menyerbu saat irq1 selesai.
     *
     * Sekarang keyboard_handler HANYA melakukan key_push ke buffer (cepat, <10 instruksi).
     * Shell processing (termasuk drawing) terjadi di sini dengan IF=1 sehingga
     * timer interrupt bisa masuk kapan saja — task switch aman, tidak ada crash. */
    while (1) {
        char c = keyboard_getchar();
        if (c) {
            if (g_gui_mode) {
                /* GUI mode: keyboard hanya ke window manager */
                wm_key_event(c);
            } else {
                /* Console mode */
                shell_process_char(c);
            }
        }
    }
}