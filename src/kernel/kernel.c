/*kernel.c - kernel utama*/
/*alamat VGA text buffer */
#define VGA_ADDRESS 0xB8000
#define VGA_COLS 80
#define VGA_ROWS 25
#define WHITE_ON_BLACK 0x0F

/*pointer ke VGA buffer - setiap entry = 2 byte (char + warna)*/
unsigned short* vga = (unsigned short*) VGA_ADDRESS;//pointer ke VGA buffer, di-cast ke unsigned short* karena setiap entry 2 byte

/* posisi kursor saat ini*/
int cursor_col = 0;
int cursor_row = 0;

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
        return;
    }
    int index = cursor_row * VGA_COLS + cursor_col;  //hitung index di VGA buffer
    vga[index] = (WHITE_ON_BLACK << 8) | c; //set entry = warna + char
    
    cursor_col++; //pindah ke kolom berikutnya
    if (cursor_col >= VGA_COLS)//jika mencapai akhir baris
    {
        cursor_col = 0; //kembali ke kolom pertama
        cursor_row++; //pindah ke baris berikutnya
    }
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

/*entry point kernel - dipanggil dari kernel_entry.asm*/
void kernel_main(){
    clear_screen();
    print("=================================");
    print("\n   Selamat datang di MyOS!   \n");
    print("=================================");
    print("\nKernel berjalan di Protected Mode (32-bit)\n");
    print("VGA text mode aktif.\n");

    /*kernel tidak boleh return - loop selamanya*/
    while (1){}
}