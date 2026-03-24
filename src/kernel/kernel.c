/*kernel.c - kernel utama*/
#include "idt.h"
#include "pic.h"
#include "shell.h"
#include "memory.h"
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
#include "ipc.h"
#include "semaphore.h"
#include "pipe.h"
#include "device.h"
#include "drv_vga.h"
#include "drv_kbd.h"
#include "graphics.h"
#include "vbe.h"
#include "keyboard.h"

/* Bochs VBE 1280x720 @ 32bpp: font 8x8 = 160 kolom x 90 baris */
#define VGA_COLS 160
#define VGA_ROWS 90

/* posisi kursor teks saat ini (dalam satuan sel karakter 8x8) */
int cursor_col = 0;
int cursor_row = 0;
int input_start_row = 0;
int input_start_col = 0;
void scroll();
void update_cursor();
void itoa(uint32_t num, char *buf);

/* Warna teks saat ini (32bpp True Color) */
uint8_t current_color = 0x0f;  /* legacy compat drv_vga */
uint32_t fg_color = GFX_LGRAY;  /* warna foreground karakter */
uint32_t bg_color = GFX_BLACK;  /* warna background sel */

void vga_put_char_at(int col, int row, char c, uint32_t color);

void set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

/*fungsi: hapus seluruh layar*/
void clear_screen() {
    fill_screen(bg_color);   /* isi framebuffer Mode 13h dengan warna background */
    cursor_col = 0;
    cursor_row = 0;
}

/*fungsi: cetak suatu karakter ke layar*/
void print_char(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= VGA_ROWS) scroll();
        return;
    }
    draw_char_gfx(cursor_col * 8, cursor_row * 8, c, fg_color, bg_color);
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
    draw_char_gfx(cursor_col * 8, cursor_row * 8, ' ', fg_color, bg_color);
    update_cursor();
}

void scroll() {
    /* Geser framebuffer ke atas 8 baris — 32bpp: tiap word = 1 piksel.
     * stride = SCREEN_W (1280 word per baris) */
    uint32_t *fb32  = (uint32_t *)FB_ADDR;
    uint32_t stride = SCREEN_W;  /* uint32_t per baris (32bpp) */
    int i;
    /* Copy baris 8..479 ke baris 0..471 */
    for (i = 0; i < (int)(stride * (SCREEN_H - 8)); i++)
        fb32[i] = fb32[i + stride * 8];
    /* Hapus 8 baris terakhir dengan warna background */
    for (i = (int)(stride * (SCREEN_H - 8)); i < (int)(stride * SCREEN_H); i++)
        fb32[i] = bg_color;
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

void enter_usermode(uint32_t eip, uint32_t user_esp) {
    __asm__ volatile (
        "mov $0x23, %%ax\n" //segment selector untuk user data (GDT entry 4)
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "push $0x23\n" //segment selector untuk user data
        "push %1\n" //stack pointer untuk user mode
        "pushf\n" //eflags
        "pop %%eax\n"
        "or $0x200, %%eax\n" //set IF flag
        "push %%eax\n"
        "pushl $0x1B\n" //segment selector untuk user code (GDT entry 3)
        "pushl %0\n" //eip untuk user mode
        "iret\n" //lakukan iret untuk switch ke user mode
        :: "r"(eip), "r"(user_esp) : "eax"
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
}

/* Deklarasi handler dari isr.asm */
extern void irq0();
extern void irq1();
extern void int80_handler();
extern void exc0();  extern void exc1();  extern void exc2();
extern void exc3();  extern void exc4();  extern void exc5();
extern void exc6();  extern void exc7();  extern void exc8();
extern void exc9();  extern void exc10(); extern void exc11();
extern void exc12(); extern void exc13(); extern void exc14();
/* Dari task.c */
extern uint32_t task_get_esp0(int id);

/* Cetak nilai 32-bit dalam format hex 8 digit */
static void print_hex32(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    char buf[9];
    int i;
    for (i = 0; i < 8; i++) {
        buf[7 - i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[8] = '\0';
    print(buf);
}

/* Dipanggil dari exc_common di isr.asm saat CPU exception terjadi.
 * Tampilkan layar merah dengan info exception, lalu halt sistem. */
void exception_handler(uint32_t exc_num, uint32_t error_code,
                       uint32_t eip,      uint32_t cr2) {
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
    itoa(exc_num, num_buf); print(num_buf);
    print(")\nErr Code  : 0x"); print_hex32(error_code);
    print("\nEIP       : 0x"); print_hex32(eip);

    if (exc_num == 14) {   /* Page Fault — CR2 berisi alamat yang menyebabkan fault */
        print("\nCR2       : 0x"); print_hex32(cr2);
        print("\nAccess    : ");
        print(error_code & 4 ? "User " : "Kernel ");
        print(error_code & 2 ? "Write "  : "Read ");
        print(error_code & 1 ? "(Protection Violation)" : "(Page Not Present)");
    }

    print("\n\nSystem Halted.\n");
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

/*entry point kernel - dipanggil dari kernel_entry.asm*/
void kernel_main(){
    /* Diagnostik: tulis ke VGA text buffer (0xB8000) sebelum VBE aktif.
     * Jika huruf 'K' muncul di pojok kiri atas, berarti kernel C sudah dicapai.
     * Ini menggunakan physical address 0xB8000 yang selalu ter-identity-map (< 4MB). */
    volatile uint16_t *vga_dbg = (volatile uint16_t *)0xB8000;
    vga_dbg[0] = 0x0F4B; /* 'K' putih di atas hitam */
    vga_dbg[1] = 0x0F43; /* 'C' */
    vga_dbg[2] = 0x0F20; /* ' ' */

    /* 1. Paging harus aktif lebih dulu agar bisa akses VBE LFB */
    paging_init();
    vga_dbg[3] = 0x0F50; /* 'P' = paging OK */

    /* 2. Temukan alamat BAR0 (VRAM/LFB) VBE stdvga via PCI config space.
     *    BAR0 (offset 0x10) = VRAM / Linear Framebuffer.
     *    BAR2 (offset 0x18) = MMIO registers (4KB) — menulis framebuffer ke sana
     *    menyebabkan machine check → triple fault → reboot loop. */
    uint32_t lfb_addr = vbe_find_lfb();
    vga_dbg[4] = 0x0F46; /* 'F' = find_lfb OK */
    paging_map_vbe(lfb_addr);
    vga_dbg[5] = 0x0F4D; /* 'M' = map OK */

    /* 3. Set mode grafis 1280x720, update pointer framebuffer, inisialisasi */
    vbe_set_mode(1280, 720, 32);
    graphics_set_fb(lfb_addr);
    graphics_init();
    clear_screen();
    print("=================================");
    print("\n   Selamat datang di MyOS!   \n");
    print("=================================");
    print("\nKernel berjalan di Protected Mode (32-bit)\n");
    shell_init();
    mem_init();
    pmm_init();
    ata_init();
    fs_init();
    ipc_init();
    sem_init_all();
    pipe_init_all();
    // Daftarkan dan inisialisasi device driver
    dev_register(DEV_VGA, &drv_vga);
    dev_register(DEV_KBD, &drv_kbd);
    dev_init_all();
    programs_init();
    timer_init(100);
    pic_init();
    idt_init();
    idt_set_gate(32, (uint32_t)irq0);
    idt_set_gate(33, (uint32_t)irq1);

    /* Exception handlers INT 0-14 — tanpa ini CPU exception → triple fault → reboot */
    idt_set_gate(0,  (uint32_t)exc0);
    idt_set_gate(1,  (uint32_t)exc1);
    idt_set_gate(2,  (uint32_t)exc2);
    idt_set_gate(3,  (uint32_t)exc3);
    idt_set_gate(4,  (uint32_t)exc4);
    idt_set_gate(5,  (uint32_t)exc5);
    idt_set_gate(6,  (uint32_t)exc6);
    idt_set_gate(7,  (uint32_t)exc7);
    idt_set_gate(8,  (uint32_t)exc8);
    idt_set_gate(9,  (uint32_t)exc9);
    idt_set_gate(10, (uint32_t)exc10);
    idt_set_gate(11, (uint32_t)exc11);
    idt_set_gate(12, (uint32_t)exc12);
    idt_set_gate(13, (uint32_t)exc13);
    idt_set_gate(14, (uint32_t)exc14);

    input_start_row = cursor_row;
    input_start_col = cursor_col;

    // inisialisasi multitasking
    task_init();
    task_set_main(); //tandai task utama sudah ada
    /* TSS esp0 harus menunjuk ke puncak kernel stack task 0 (shell),
     * sehingga saat ring-3 → ring-0 transition, CPU memakai stack yang benar. */
    tss_init(task_get_esp0(0));
    //register syscall dengan DPL = 3 agar ring-3 bisa memanggil int 0x80
    idt_set_gate_user(0x80, (uint32_t)int80_handler);

    /* IRQ12 — PS/2 Mouse (INT 44 = slave IRQ4) */
    extern void irq12();
    idt_set_gate(44, (uint32_t)irq12);
    wm_init();
    mouse_init();
    /* Tidak membuat background task — task_count=1, task_switch selalu early return.
     * Background task menyebabkan task switch aktif, yang mengubah CR3 dan ESP
     * secara tidak terduga saat shell berada di tengah drawing loop → crash. */

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
            if (wm_has_focus())
                wm_key_event(c);   /* kirim ke window yang sedang fokus */
            else
                shell_process_char(c);
        }
    }
}