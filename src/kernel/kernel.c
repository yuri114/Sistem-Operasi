/*kernel.c - kernel utama*/
/*alamat VGA text buffer */
#include "idt.h"
#include "pic.h"
#include "shell.h"
#include "memory.h"
#include "timer.h"

#define VGA_ADDRESS 0xB8000
#define VGA_COLS 80
#define VGA_ROWS 25
#define WHITE_ON_BLACK 0x0F

/*pointer ke VGA buffer - setiap entry = 2 byte (char + warna)*/
unsigned short* vga = (unsigned short*) VGA_ADDRESS;//pointer ke VGA buffer, di-cast ke unsigned short* karena setiap entry 2 byte

/* posisi kursor saat ini*/
int cursor_col = 0;
int cursor_row = 0;
int input_start_row = 0; //baris awal untuk input keyboard
int input_start_col = 0; //kolom awal untuk input keyboard
void scroll(); //fungsi untuk menggulir layar ke atas saat mencapai akhir layar
void update_cursor(); //fungsi untuk update posisi kursor di hardware
void itoa(uint32_t num, char *buf); //fungsi untuk konversi integer ke string

/*fungsi: hapus seluruh layar*/
void clear_screen() {
    int i;
    for (i = 0; i < VGA_COLS * VGA_ROWS; i++)   //loop untuk setiap entry di VGA buffer
    {
        vga[i] = (WHITE_ON_BLACK << 8) | ' ';   //setiap entry = warna + char
    }
    cursor_col = 0;
    cursor_row = 0;
}

/*fungsi: cetak suatu karakter ke layar*/
void print_char(char c){
    if (c=='\n') //jika karakter newline, pindah ke baris berikutnya
    {
        cursor_col = 0;//kembali ke kolom pertama
        cursor_row++;//pindah ke baris berikutnya
        if (cursor_row >= VGA_ROWS)
        {
            scroll(); //geser layar ke atas jika mencapai akhir layar
        }
        
        return;
    }
    int index = cursor_row * VGA_COLS + cursor_col;  //hitung index di VGA buffer
    vga[index] = (WHITE_ON_BLACK << 8) | c; //set entry = warna + char
    
    cursor_col++; //pindah ke kolom berikutnya
    if (cursor_col >= VGA_COLS)//jika mencapai akhir baris
    {
        cursor_col = 0; //kembali ke kolom pertama
        cursor_row++; //pindah ke baris berikutnya
        if (cursor_row >= VGA_ROWS)
        {
            scroll(); //geser layar ke atas jika mencapai akhir layar
        }
    }
    update_cursor(); //update posisi kursor di hardware
}

void backspace_char(){
    if (cursor_row == input_start_row && cursor_col == input_start_col) return; //jika sudah di awal layar, tidak lakukan apa-apa
    
    if (cursor_col==0)
    {
        cursor_row--;
        cursor_col = VGA_COLS - 1; //kembali ke kolom terakhir baris sebelumnya
    }
    else
    {
        cursor_col--;
    }
    int index = cursor_row * VGA_COLS + cursor_col; //hitung index di VGA buffer
    vga[index] = (WHITE_ON_BLACK << 8)| ' '; //hapus karakter dengan spasi
    update_cursor(); //update posisi kursor di hardware
}

void scroll() {
    int row, col;
    for (row = 1; row < VGA_ROWS; row++)
    {
        for (col = 0; col < VGA_COLS; col++)
        {
            vga[(row - 1) * VGA_COLS + col] = vga[row * VGA_COLS + col]; //geser setiap baris ke atas
        }
    }
    for (col = 0; col < VGA_COLS; col++)
    {
        vga[(VGA_ROWS - 1) * VGA_COLS + col] = (WHITE_ON_BLACK << 8) | ' '; //hapus baris terakhir
    }
    cursor_row = VGA_ROWS - 1; //pindah ke baris terakhir
}

static inline void outb(uint16_t port, uint8_t value){
    __asm__ volatile ("outb %0, %1":: "a"(value), "Nd"(port));
}

void update_cursor(){
    uint16_t pos = cursor_row * VGA_COLS + cursor_col; /* hitung posisi kursor */
    outb(0x3D4, 0x0F); //index register untuk low byte
    outb(0x3D5,pos & 0xFF); //kirim low byte
    
    outb(0x3D4, 0x0E); //index register untuk high byte
    outb(0x3D5, (pos >> 8) & 0xFF); //kirim high byte
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

/* Deklarasi handler dari isr.asm */
extern void irq0();
extern void irq1();

/*entry point kernel - dipanggil dari kernel_entry.asm*/
void kernel_main(){
    clear_screen();
    print("=================================");
    print("\n   Selamat datang di MyOS!   \n");
    print("=================================");
    print("\nKernel berjalan di Protected Mode (32-bit)\n");
    shell_init(); //inisialisasi shell
    mem_init();   //inisialisasi manajemen memori
    timer_init(100); //inisialisasi timer dengan frekuensi 100Hz
    pic_init();                          /* remap PIC dulu */
    idt_init();                          /* inisialisasi IDT (semua entry = 0) */
    idt_set_gate(32, (uint32_t)irq0);   /* timer (IRQ0 = interrupt 32) - harus ada! */
    idt_set_gate(33, (uint32_t)irq1);   /* keyboard (IRQ1 = interrupt 33) */

    input_start_row = cursor_row; //simpan posisi awal input keyboard
    input_start_col = cursor_col;

    /* aktifkan hardware interrupt */
    __asm__ volatile ("sti");

    /*kernel tidak boleh return - loop selamanya*/
    while (1){}
}