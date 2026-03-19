#include "shell.h"
#include "memory.h"
#include "timer.h"
/*fungsi dari kernel.c*/
void print(const char *str);
void print_char(char c);
void clear_screen();
void backspace_char();
void itoa(uint32_t num, char *buf);
void set_color(uint8_t fg, uint8_t bg);

/* Buffer untuk menyimpan input dari keyboard */
static char input_buffer[256];
static int input_len = 0;

static int str_compare(const char *a, const char *b){
    int i=0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return 0; //tidak sama
        i++;
    }
    return a[i] == '\0' && b[i] == '\0'; //sama jika kedua string berakhir bersamaan
}
static int str_starts_with(const char *str, const char *prefix){
    int i=0;
    while (prefix[i] != '\0') {
        if (str[i] != prefix[i]) return 0; //tidak sama
        i++;
    }
    return 1; //sama jika prefix habis
}

void outb(uint16_t port, uint8_t val){
    __asm__ volatile ("outb %0, %1":: "a"(val), "Nd"(port));
} //deklarasi fungsi outb dari kernel.c

static void shell_execute(){
    print("\n");

    if(str_compare(input_buffer, "help")){
        set_color(14,0); //warna kuning di hitam untuk judul
        print("Perintah yang tersedia:\n");
        set_color(15,0); //warna putih di hitam untuk daftar perintah
        print("help        - tampilkan daftar perintah\n");
        print("clear       - bersihkan layar\n");
        print("about       - informasi tentang myOS\n");
        print("memtest     - test alokasi memory\n");
        print("uptime      - tampilkan waktu berjalan OS\n");
        print("echo <text> - tampilkan text\n");
        print("time        - tampilkan ticks sejak boot\n");
        print("reboot      - reboot sistem\n");
    }
    else if(str_compare(input_buffer, "clear")){
        clear_screen();
    }
    else if(str_compare(input_buffer, "about")){
        print("MyOS versi 0.1\n");
        print("Sistem operasi sederhana untuk belajar\n");
    }
    else if(str_compare(input_buffer, "memtest")){
        char *buf = (char*) malloc(16);
        if (buf==0) {
            print("malloc gagal!\n");
        }
        else {
            print("malloc berhasil! memori dialokasikan.\n");
        }
    }
    else if(str_compare(input_buffer, "uptime")){
        char buf[16];
        itoa(get_ticks() / 100, buf);
        print("uptime: ");
        print(buf);
        print(" detik\n");
    }
    else if(str_compare(input_buffer, "echo")){
        print("Gunakan: echo <text>\n");
    }
    else if(str_starts_with(input_buffer, "echo ")){
        print(input_buffer + 5);
        print("\n");
    }
    else if(str_compare(input_buffer, "time")){
        char buf[20];
        uint32_t ticks = get_ticks();
        print ("Ticks sejak boot: ");
        itoa(ticks, buf);
        print(buf);
        print("\n");
    }
    else if(str_compare(input_buffer, "reboot")){
        print("Rebooting...\n");
        outb(0x64, 0xFE); //perintah reboot ke keyboard controller
    }
    else {
        set_color(12,0); //warna merah di hitam untuk error
        print("Perintah tidak dikenal: ");
        print(input_buffer);
        print("\n");
        set_color(15,0); //kembalikan warna putih di hitam
    }
}

void shell_init(){
    set_color(14,0); //warna kuning di hitam
    print("\nKetik 'help' untuk daftar perintah\n");
    set_color(10,0); //warna hijau di hitam
    print("> "); //prompt
    set_color(15,0); //warna putih di hitam untuk input
}

void shell_process_char(char c){
    if (c=='\n'){                           /* enter ditekan*/
        input_buffer[input_len] = '\0';     /* tutup string dengan null terminator*/
        input_len = 0;                      /* reset buffer untuk input berikutnya*/        
        shell_execute();                    /* eksekusi perintah */
        set_color(10,0);                     /* warna hijau di hitam untuk prompt */
        print("> ");                        /* tampilkan prompt */
        set_color(15,0);                     /* warna putih di hitam untuk input */
    }
    else if(c== '\b'){                      /* backspace */
        if (input_len > 0) {
            input_len--;                    /* hapus karakter terakhir dari buffer */
            backspace_char();               /* hapus karakter dari layar */
        }
    }
    else {
        if (input_len < 255){
            input_buffer[input_len] = c;    /* simpan karakter ke buffer */
            input_len++;
            print_char(c);                  /* tampilkan karakter ke layar */
        }
    }
}