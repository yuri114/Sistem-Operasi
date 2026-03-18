#include "shell.h"
#include "memory.h"
#include "timer.h"
/*fungsi dari kernel.c*/
void print(const char *str);
void print_char(char c);
void clear_screen();
void backspace_char();
void itoa(uint32_t num, char *buf);

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

static void shell_execute(){
    print("\n");

    if(str_compare(input_buffer, "help")){
        print("Perintah yang tersedia:\n");
        print("help      - tampilkan daftar perintah\n");
        print("clear     - bersihkan layar\n");
        print("about     - informasi tentang myOS\n");
        print("memtest   - test alokasi memory\n");
        print("uptime    - tampilkan waktu berjalan OS\n");
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
    else {
        print("Perintah tidak dikenal: ");
        print(input_buffer);
        print("\n");
    }
}

void shell_init(){
    print("\nKetik 'help' untuk daftar perintah\n");
    print("> "); //prompt
}

void shell_process_char(char c){
    if (c=='\n'){                           /* enter ditekan*/
        input_buffer[input_len] = '\0';     /* tutup string dengan null terminator*/
        shell_execute();                    /* eksekusi perintah */
        input_len = 0;                      /* reset buffer untuk input berikutnya*/
        print("> ");                        /* tampilkan prompt */
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