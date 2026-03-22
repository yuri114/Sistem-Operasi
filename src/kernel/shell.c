#include "shell.h"
#include "memory.h"
#include "timer.h"
#include "fs.h"
#include "paging.h"
#include "vmm.h"
#include "elf_loader.h"
#include "task.h"

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

/* History ring buffer */
#define HISTORY_SIZE 8
static char history[HISTORY_SIZE][256];
static int hist_head  = 0;  // slot berikutnya untuk ditulis
static int hist_count = 0;  // jumlah entri tersimpan (maks HISTORY_SIZE)
static int hist_cursor = -1; // -1 = tidak browse; 0 = paling baru, 1 = sebelumnya

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

static int str_find_space(const char *str) {
    int i=0;
    while (str[i] != '\0') {
        if (str[i] == ' ')
            return i; //kembalikan index spasi pertama
        i++;
    }
    return -1; //tidak ditemukan
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
        print("help                 - tampilkan daftar perintah\n");
        print("clear                - bersihkan layar\n");
        print("about                - informasi tentang myOS\n");
        print("memtest              - test alokasi memory\n");
        print("uptime               - tampilkan waktu berjalan OS\n");
        print("echo <text>          - tampilkan text\n");
        print("time                 - tampilkan ticks sejak boot\n");
        print("reboot               - reboot sistem\n");
        print("ls                   - tampilkan daftar file\n");
        print("read <nama>          - baca file\n");
        print("write <nama> <isi>   - simpan file\n");
        print("del <nama>           - hapus file\n");
        print("paging               - tampilkan status paging\n");
        print("exec <nama>          - jalankan program ELF\n");
        print("ps                   - tampilkan daftar proses\n");
        print("kill <id>            - matikan proses berdasarkan ID\n");
    }
    else if(str_compare(input_buffer, "clear")){
        clear_screen();
    }
    else if(str_compare(input_buffer, "about")){
        print("MyOS versi 0.0.3\n");
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
    else if(str_compare(input_buffer, "ls")){
        set_color(14,0); //warna kuning di hitam untuk judul
        print("Daftar file:\n");
        set_color(15,0); //warna putih di hitam untuk daftar file
        fs_list(print); //panggil fs_list dengan fungsi print sebagai callback untuk menampilkan nama file   
    }
    else if(str_starts_with(input_buffer, "read ")) {
        const char *name = input_buffer + 5; //ambil nama file setelah "read "
        const char *data = fs_read(name);
        if (data) {
            print(data);
            print("\n");
        }
        else {
            set_color(12,0); //warna merah di hitam untuk error
            print("File tidak ditemukan: ");
            print(name);
            print("\n");
            set_color(15,0); //kembalikan warna putih di hitam
        }
    }
    else if (str_starts_with(input_buffer, "del ")) {
        const char *name = input_buffer + 4;
        if (fs_delete(name)) {
            set_color(10, 0);
            print("File dihapus: ");
            print(name);
            print("\n");
            set_color(15,0); //kembalikan warna putih di hitam
        }
        else {
            set_color(12,0); //warna merah di hitam untuk error
            print("File tidak ditemukan: ");
            print(name);
            print("\n");
            set_color(15,0); //kembalikan warna putih di hitam
        }
    }
    else if(str_compare(input_buffer, "write")){
        print("gunakan write <nama> <isi>\n");
    }
    else if(str_starts_with(input_buffer, "write ")){
        const char *rest = input_buffer + 6;
        int space = str_find_space(rest);
        if (space < 0) {
            print("Gunakan: write <nama> <isi>\n");
        }
        else {
            char name [32];
            int i;
            for (i=0; i<space && i<31; i++) {
                name[i] = rest[i];
            }
            name[i] = '\0'; //tutup string nama dengan null terminator
            const char *data = rest + space + 1; //ambil isi setelah spasi
            if (fs_write(name, data)) {
                set_color(10, 0);
                print("File disimpan: ");
                print(name);
                print("\n");
                set_color(15,0); //kembalikan warna putih di hitam
            }
            else {
                set_color(12,0); //warna merah di hitam untuk error
                print("Filesystem penuh!");
                print("\n");
                set_color(15,0); //kembalikan warna putih di hitam
            }
        }
    }
    else if (str_compare(input_buffer, "paging")) {
        uint32_t cr0 = paging_get_cr0();
        char buf[20];
        if (cr0 & 0x80000000) {
            set_color(10,0); //warna hijau di hitam untuk info
            print("Paging aktif");
        }
        else {
            set_color(12,0); //warna merah di hitam untuk error
            print("Paging tidak aktif");
        }
        print("\nCR0: 0x");
        itoa(cr0, buf);
        print(buf);
        print("\n");
        set_color(15,0); //kembalikan warna putih di hitam
    }
    else if(str_starts_with(input_buffer, "exec ")) {
        const char *name = input_buffer + 5; // ambil nama setelah "exec "
        uint32_t size;
        const uint8_t *data = fs_read_bin(name, &size);
        if (!data) {
            print("exec: file tidak ditemukan\n");
        } else {
            // buat page directory baru khusus untuk proses ini (isolasi penuh)
            uint32_t *proc_dir = vmm_create_page_dir();
            uint32_t entry = elf_load(data, size, proc_dir);
            if (!entry) {
                print("exec: gagal memuat ELF\n");
            } else {
                // alokasikan user stack dari PMM, petakan di virtual 0x400000
                uint32_t stack_phys = pmm_alloc_frame();
                vmm_map_page(proc_dir, 0x400000, stack_phys, 7);
                uint32_t user_esp = 0x400000 + PAGE_SIZE; // puncak stack (tumbuh ke bawah)
                task_create_user(entry, proc_dir, user_esp, name);
                print("exec: program dimulai\n");
            }
        }
    }
    else if(str_compare(input_buffer, "ps")) {
        int i;
        int cur = task_get_current();
        set_color(14, 0);
        print("ID  STATUS    NAMA\n");
        print("--- --------- ----------------\n");
        set_color(15, 0);
        for (i = 0; i < task_get_max(); i++) {
            if (!task_is_used(i)) continue;
            char id_buf[4];
            itoa(i, id_buf);
            // pad ID ke 3 karakter
            print(id_buf);
            print("   ");
            if (i == cur) {
                set_color(10, 0);
                print("running   ");
            } else {
                set_color(7, 0);
                print("ready     ");
            }
            set_color(15, 0);
            print(task_get_name(i));
            print("\n");
        }
    }
    else if(str_starts_with(input_buffer, "kill ")) {
        // parse angka id dari "kill <id>"
        const char *p = input_buffer + 5;
        int id = 0;
        while (*p >= '0' && *p <= '9') {
            id = id * 10 + (*p - '0');
            p++;
        }
        if (id == 0) {
            set_color(12, 0);
            print("kill: tidak dapat mematikan shell (id 0)\n");
            set_color(15, 0);
        } else if (task_kill(id)) {
            set_color(10, 0);
            print("kill: proses ");
            char buf[8]; itoa(id, buf); print(buf);
            print(" dihentikan\n");
            set_color(15, 0);
        } else {
            set_color(12, 0);
            print("kill: proses tidak ditemukan atau tidak dapat dimatikan\n");
            set_color(15, 0);
        }
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
        input_buffer[input_len] = '\0';     /* tutup string */

        // simpan ke history jika bukan kosong
        if (input_len > 0) {
            int i;
            for (i = 0; i <= input_len; i++) history[hist_head][i] = input_buffer[i];
            hist_head = (hist_head + 1) % HISTORY_SIZE;
            if (hist_count < HISTORY_SIZE) hist_count++;
            hist_cursor = -1;
        }

        input_len = 0;
        shell_execute();
        set_color(10,0);
        print("> ");
        set_color(15,0);
    }
    else if(c== '\b'){                      /* backspace */
        if (input_len > 0) {
            input_len--;
            backspace_char();
        }
    }
    else if (c == '\x01') {                 /* ↑ up arrow: maju ke history lebih lama */
        if (hist_count == 0) return;
        if (hist_cursor == -1) hist_cursor = 0;
        else if (hist_cursor < hist_count - 1) hist_cursor++;
        else return; // sudah di entri tertua
        // hapus input sekarang dan tampilkan entri history
        int i;
        for (i = 0; i < input_len; i++) backspace_char();
        int idx = (hist_head - 1 - hist_cursor + HISTORY_SIZE * 8) % HISTORY_SIZE;
        i = 0;
        while (history[idx][i] && i < 255) {
            input_buffer[i] = history[idx][i];
            print_char(history[idx][i]);
            i++;
        }
        input_buffer[i] = '\0';
        input_len = i;
    }
    else if (c == '\x02') {                 /* ↓ down arrow: kembali ke history lebih baru */
        if (hist_cursor < 0) return;
        int i;
        for (i = 0; i < input_len; i++) backspace_char();
        if (hist_cursor == 0) {
            // kembali ke input kosong
            hist_cursor = -1;
            input_buffer[0] = '\0';
            input_len = 0;
        } else {
            hist_cursor--;
            int idx = (hist_head - 1 - hist_cursor + HISTORY_SIZE * 8) % HISTORY_SIZE;
            i = 0;
            while (history[idx][i] && i < 255) {
                input_buffer[i] = history[idx][i];
                print_char(history[idx][i]);
                i++;
            }
            input_buffer[i] = '\0';
            input_len = i;
        }
    }
    else {
        if (input_len < 255){
            hist_cursor = -1; // keluar dari mode browse saat user mengetik
            input_buffer[input_len] = c;
            input_len++;
            print_char(c);
        }
    }
}