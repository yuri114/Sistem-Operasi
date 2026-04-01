/* shell.c — Command-line shell: input, history, tab-completion, built-in commands */
#include "shell.h"
#include "graphics.h"
#include "memory.h"
#include "timer.h"
#include "fs.h"
#include "paging.h"
#include "vmm.h"
#include "elf_loader.h"
#include "task.h"
#include "pipe.h"
#include "net.h"
#include "acpi.h"
#include "smp.h"
#include "vfs.h"
#include "mq.h"

/*fungsi dari kernel.c*/
void print(const char *str);
void print_char(char c);
void clear_screen();
void backspace_char();
void itoa(uint32_t num, char *buf);
void set_color(uint32_t fg, uint32_t bg);

/* Buffer untuk menyimpan input dari keyboard */
static char input_buffer[256];
static int input_len = 0;

/* History ring buffer */
#define HISTORY_SIZE 8
static char history[HISTORY_SIZE][256];
static int hist_head  = 0;  // slot berikutnya untuk ditulis
static int hist_count = 0;  // jumlah entri tersimpan (maks HISTORY_SIZE)
static int hist_cursor = -1; // -1 = tidak browse; 0 = paling baru, 1 = sebelumnya

/* F1 — Direktori kerja virtual */
static char current_dir[64] = "";

/* F1 — Environment variables */
#define ENV_MAX 16
static char env_keys[ENV_MAX][24];
static char env_vals[ENV_MAX][64];
static int  env_count = 0;

static int str_compare(const char *a, const char *b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}
static int str_starts_with(const char *str, const char *prefix) {
    int i = 0;
    while (prefix[i] != '\0') {
        if (str[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static int str_find_space(const char *str) {
    int i=0;
    while (str[i] != '\0') {
        if (str[i] == ' ')
            return i;
        i++;
    }
    return -1;
}

// Daftar semua perintah untuk tab-completion
static const char *shell_commands[] = {
    "help", "clear", "about", "memtest", "uptime",
    "time", "reboot", "ls", "paging", "ps",
    "echo ", "exec ", "read ", "write ", "del ", "kill ",
    "cd ", "pwd", "export ", "env",
    "sync", "mkdir ", "chmod ",
    "ifconfig", "ping ", "cpuinfo",
    "open ", "fread ", "fwrite ", "fclose ",
    "mq_send ", "mq_recv", "taskstat", "meminfo", "threadtest",
    0
};

// Panjang string (tanpa null)
static int str_len(const char *s) {
    int i = 0; while (s[i]) i++; return i;
}

// Salin src ke dst, kembalikan panjang
static int str_copy(char *dst, const char *src) {
    int i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return i;
}

/* G4 — Parse "a.b.c.d" ke uint8_t[4]. Return 1 berhasil, 0 gagal. */
static int parse_ip(const char *s, uint8_t ip[4]) {
    int i;
    for (i = 0; i < 4; i++) {
        int n = 0;
        while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
        if (n > 255) return 0;
        ip[i] = (uint8_t)n;
        if (i < 3) { if (*s != '.') return 0; s++; }
    }
    return 1;
}

/* F1 — Buat path: prefix current_dir jika perlu */
static const char *make_path(const char *name, char *buf, int bufsz) {
    if (current_dir[0] == '\0') return name;
    /* Jika name sudah ada '/'  → absolut, pakai apa adanya */
    int i = 0;
    while (name[i] && name[i] != '/') i++;
    if (name[i] == '/') return name;
    /* Prefix: current_dir/name */
    int di = 0;
    while (current_dir[di] && di < bufsz - 2) { buf[di] = current_dir[di]; di++; }
    buf[di++] = '/';
    int ni = 0;
    while (name[ni] && di < bufsz - 1) buf[di++] = name[ni++];
    buf[di] = '\0';
    return buf;
}

/* F1 — Ekspansi $VAR dalam input_buffer in-place */
static void shell_expand_vars(void) {
    char tmp[256];
    int ti = 0, ii = 0;
    while (input_buffer[ii] && ti < 254) {
        if (input_buffer[ii] == '$') {
            ii++;
            char vname[24]; int vi = 0;
            while (((input_buffer[ii] >= 'A' && input_buffer[ii] <= 'Z') ||
                    (input_buffer[ii] >= 'a' && input_buffer[ii] <= 'z') ||
                    (input_buffer[ii] >= '0' && input_buffer[ii] <= '9') ||
                     input_buffer[ii] == '_') && vi < 23) {
                vname[vi++] = input_buffer[ii++];
            }
            vname[vi] = '\0';
            int e;
            for (e = 0; e < env_count; e++) {
                if (str_compare(env_keys[e], vname)) {
                    char *v = env_vals[e];
                    while (*v && ti < 254) tmp[ti++] = *v++;
                    break;
                }
            }
            /* variabel tidak ditemukan → kosong */
        } else {
            tmp[ti++] = input_buffer[ii++];
        }
    }
    tmp[ti] = '\0';
    { int i; for (i = 0; i <= ti; i++) input_buffer[i] = tmp[i]; }
    input_len = ti;
}

static void shell_tab_complete() {
    if (input_len == 0) return;
    input_buffer[input_len] = '\0';

    // --- Cek apakah sedang mengetik argumen file (exec/read/del) ---
    int file_mode = 0;
    int arg_offset = 0; // posisi awal argumen dalam input_buffer
    if (str_starts_with(input_buffer, "exec ")) { file_mode = 1; arg_offset = 5; }
    else if (str_starts_with(input_buffer, "read ")) { file_mode = 1; arg_offset = 5; }
    else if (str_starts_with(input_buffer, "del "))  { file_mode = 1; arg_offset = 4; }

    if (file_mode) {
        // Hanya complete jika sudah ada argumen (atau prefix kosong → tampilkan semua tidak dilakukan)
        const char *prefix = input_buffer + arg_offset;
        char match[32];
        if (fs_find_prefix(prefix, match)) {
            // Hapus bagian prefix yang sudah diketik
            int prefix_len = input_len - arg_offset;
            int i;
            for (i = 0; i < prefix_len; i++) backspace_char();
            // Tulis nama file lengkap
            int added = str_copy(input_buffer + arg_offset, match);
            input_len = arg_offset + added;
            for (i = 0; i < added; i++) print_char(match[i]);
        }
        return;
    }

    // --- Complete nama perintah ---
    const char *found = 0;
    int ambiguous = 0;
    int i;
    for (i = 0; shell_commands[i]; i++) {
        // cek apakah input_buffer adalah prefix dari perintah ini
        int j = 0, match = 1;
        while (j < input_len) {
            if (!shell_commands[i][j] || shell_commands[i][j] != input_buffer[j]) {
                match = 0; break;
            }
            j++;
        }
        if (match) {
            if (found) { ambiguous = 1; break; }
            found = shell_commands[i];
        }
    }
    if (found && !ambiguous) {
        // Hapus input sekarang, tulis perintah lengkap
        for (i = 0; i < input_len; i++) backspace_char();
        int added = str_copy(input_buffer, found);
        input_len = added;
        for (i = 0; i < added; i++) print_char(found[i]);
    }
}

void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static void shell_execute(){
    print("\n");

    /* F1: ekspansi $VAR */
    shell_expand_vars();

    /* F1: strip trailing '&' → background exec flag */
    int bg_exec = 0;
    {
        int t = 0; while (input_buffer[t]) t++; t--;
        while (t >= 0 && input_buffer[t] == ' ') t--;
        if (t >= 0 && input_buffer[t] == '&') {
            bg_exec = 1;
            input_buffer[t] = '\0';
            while (t > 0 && input_buffer[t-1] == ' ') { t--; input_buffer[t] = '\0'; }
        }
    }
    (void)bg_exec; /* dipakai di exec command */

    if(str_compare(input_buffer, "help")){
        set_color(GFX_YELLOW, GFX_BLACK);
        print("Perintah yang tersedia:\n");
        set_color(GFX_WHITE, GFX_BLACK);
        print("help                 - tampilkan daftar perintah\n");
        print("clear                - bersihkan layar\n");
        print("about                - informasi tentang Oria OS\n");
        print("memtest              - test alokasi memory\n");
        print("uptime               - tampilkan waktu berjalan OS\n");
        print("echo <text>          - tampilkan text\n");
        print("time                 - tampilkan ticks sejak boot\n");
        print("reboot               - reboot sistem\n");
        print("ls                   - tampilkan daftar file (di direktori saat ini)\n");
        print("cd <dir>             - pindah direktori (cd .. / cd / untuk root)\n");
        print("pwd                  - tampilkan direktori saat ini\n");
        print("read <nama>          - baca file\n");
        print("write <nama> <isi>   - simpan file\n");
        print("del <nama>           - hapus file\n");
        print("mkdir <nama>         - buat direktori\n");
        print("chmod <nama> <hex>   - ubah permission file\n");
        print("sync                 - flush dirty file ke disk\n");
        print("export KEY=VAL       - set environment variable\n");
        print("env                  - tampilkan semua env var\n");
        print("ifconfig             - tampilkan info jaringan (MAC, IP, GW)\n");
        print("ping <ip>            - kirim 4 ICMP echo request ke IP\n");
        print("cpuinfo              - tampilkan info SMP (BSP/AP online)\n");
        print("taskstat             - tampilkan distribusi task per CPU\n");
        print("meminfo              - tampilkan statistik memori fisik & heap\n");
        print("threadtest           - demo threading: spawn 3 thread secara paralel\n");
        print("open <file>          - buka file, cetak fd\n");
        print("fread <fd>           - baca isi file lewat fd\n");
        print("fwrite <fd> <teks>   - tulis teks ke file lewat fd\n");
        print("fclose <fd>          - tutup fd\n");
        print("mq_send <pid> <msg>  - kirim pesan ke task pid\n");
        print("mq_recv              - terima pesan dari mailbox shell\n");
        print("paging               - tampilkan status paging\n");
        print("exec <nama> [&]      - jalankan program ELF (& = background)\n");
        print("ps                   - tampilkan daftar proses\n");
        print("kill <id>            - matikan proses berdasarkan ID\n");
        print("setprio <id> <1-3>   - ubah priority proses\n");
    }
    else if(str_compare(input_buffer, "clear")){
        clear_screen();
    }
    else if(str_compare(input_buffer, "about")){
        print("Oria OS versi 0.1.0\n");
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
        outb(0x64, 0xFE);
    }
    else if(str_compare(input_buffer, "ls")){
        set_color(GFX_YELLOW, GFX_BLACK);
        if (current_dir[0]) {
            print("Isi direktori /"); print(current_dir); print(":\n");
        } else {
            print("Daftar file:\n");
        }
        set_color(GFX_WHITE, GFX_BLACK);
        if (current_dir[0]) fs_list_dir(current_dir, print);
        else                fs_list(print);
    }
    else if(str_compare(input_buffer, "sync")) {
        int n = fs_flush();
        char buf[8];
        set_color(GFX_LGREEN, GFX_BLACK);
        print("sync: ");
        itoa(n, buf); print(buf);
        print(" file di-flush ke disk\n");
        set_color(GFX_WHITE, GFX_BLACK);
    }
    else if(str_compare(input_buffer, "mkdir")) {
        print("gunakan mkdir <nama>\n");
    }
    else if(str_starts_with(input_buffer, "mkdir ")) {
        const char *name = input_buffer + 6;
        if (fs_mkdir(name)) {
            set_color(GFX_LGREEN, GFX_BLACK);
            print("mkdir: direktori dibuat: "); print(name); print("\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else {
            set_color(GFX_LRED, GFX_BLACK);
            print("mkdir: gagal (nama duplikat atau FS penuh)\n");
            set_color(GFX_WHITE, GFX_BLACK);
        }
    }
    else if(str_compare(input_buffer, "chmod")) {
        print("gunakan chmod <nama> <perm_hex>\n");
    }
    else if(str_starts_with(input_buffer, "chmod ")) {
        const char *rest = input_buffer + 6;
        char name[32];
        int j = 0;
        while (rest[j] && rest[j] != ' ' && j < 31) { name[j] = rest[j]; j++; }
        name[j] = '\0';
        if (rest[j] != ' ') {
            print("gunakan chmod <nama> <perm_hex>\n");
        } else {
            const char *phex = rest + j + 1;
            uint32_t perms = 0;
            int k = 0;
            while (phex[k]) {
                char ch = phex[k];
                if (ch >= '0' && ch <= '9')      perms = perms * 16 + (ch - '0');
                else if (ch >= 'a' && ch <= 'f') perms = perms * 16 + (ch - 'a' + 10);
                else if (ch >= 'A' && ch <= 'F') perms = perms * 16 + (ch - 'A' + 10);
                k++;
            }
            if (fs_set_perms(name, (uint16_t)perms)) {
                set_color(GFX_LGREEN, GFX_BLACK);
                print("chmod: permission diubah\n");
                set_color(GFX_WHITE, GFX_BLACK);
            } else {
                set_color(GFX_LRED, GFX_BLACK);
                print("chmod: file tidak ditemukan\n");
                set_color(GFX_WHITE, GFX_BLACK);
            }
        }
    }
    else if(str_starts_with(input_buffer, "read ")) {
        char pbuf[64];
        const char *name = make_path(input_buffer + 5, pbuf, 64);
        const char *data = fs_read(name);
        if (data) {
            print(data);
            print("\n");
        }
        else {
            set_color(GFX_LRED, GFX_BLACK);
            print("File tidak ditemukan: ");
            print(name);
            print("\n");
            set_color(GFX_WHITE, GFX_BLACK);
        }
    }
    else if (str_starts_with(input_buffer, "del ")) {
        char pbuf[64];
        const char *name = make_path(input_buffer + 4, pbuf, 64);
        if (fs_delete(name)) {
            set_color(GFX_LGREEN, GFX_BLACK);
            print("File dihapus: ");
            print(name);
            print("\n");
            set_color(GFX_WHITE, GFX_BLACK);
        }
        else {
            set_color(GFX_LRED, GFX_BLACK);
            print("File tidak ditemukan: ");
            print(name);
            print("\n");
            set_color(GFX_WHITE, GFX_BLACK);
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
            char rawname[32];
            int i;
            for (i=0; i<space && i<31; i++) rawname[i] = rest[i];
            rawname[i] = '\0';
            char pbuf[64];
            const char *name = make_path(rawname, pbuf, 64);
            const char *data = rest + space + 1;
            if (fs_write(name, data)) {
                set_color(GFX_LGREEN, GFX_BLACK);
                print("File disimpan: ");
                print(name);
                print("\n");
                set_color(GFX_WHITE, GFX_BLACK);
            }
            else {
                set_color(GFX_LRED, GFX_BLACK);
                print("Filesystem penuh!");
                print("\n");
                set_color(GFX_WHITE, GFX_BLACK);
            }
        }
    }
    /* F1 — Direktori */
    else if(str_compare(input_buffer, "cd")) {
        current_dir[0] = '\0';
    }
    else if(str_starts_with(input_buffer, "cd ")) {
        const char *arg = input_buffer + 3;
        if (str_compare(arg, "..") || str_compare(arg, "/") || str_compare(arg, "~")) {
            current_dir[0] = '\0';
        } else {
            int i = 0;
            while (arg[i] && i < 62) { current_dir[i] = arg[i]; i++; }
            current_dir[i] = '\0';
        }
        set_color(GFX_LCYAN, GFX_BLACK);
        print("cd: /");
        print(current_dir);
        print("\n");
        set_color(GFX_WHITE, GFX_BLACK);
    }
    else if(str_compare(input_buffer, "pwd")) {
        print("/");
        print(current_dir);
        print("\n");
    }
    /* F1 — Environment variables */
    else if(str_compare(input_buffer, "env")) {
        if (env_count == 0) {
            print("(tidak ada env var)\n");
        } else {
            int e;
            for (e = 0; e < env_count; e++) {
                print(env_keys[e]); print("="); print(env_vals[e]); print("\n");
            }
        }
    }
    else if(str_compare(input_buffer, "export")) {
        print("gunakan export KEY=VALUE\n");
    }
    else if(str_starts_with(input_buffer, "export ")) {
        const char *rest = input_buffer + 7;
        /* Cari '=' */
        int eq = 0;
        while (rest[eq] && rest[eq] != '=') eq++;
        if (rest[eq] != '=') {
            print("export: format: export KEY=VALUE\n");
        } else {
            char key[24]; int ki = 0;
            while (ki < eq && ki < 23) { key[ki] = rest[ki]; ki++; }
            key[ki] = '\0';
            const char *val = rest + eq + 1;
            /* Cari atau update */
            int e, found = 0;
            for (e = 0; e < env_count; e++) {
                if (str_compare(env_keys[e], key)) {
                    int vi = 0;
                    while (val[vi] && vi < 63) { env_vals[e][vi] = val[vi]; vi++; }
                    env_vals[e][vi] = '\0';
                    found = 1; break;
                }
            }
            if (!found) {
                if (env_count < ENV_MAX) {
                    str_copy(env_keys[env_count], key);
                    int vi = 0;
                    while (val[vi] && vi < 63) { env_vals[env_count][vi] = val[vi]; vi++; }
                    env_vals[env_count][vi] = '\0';
                    env_count++;
                } else {
                    print("export: terlalu banyak variabel\n");
                }
            }
            set_color(GFX_LGREEN, GFX_BLACK);
            print("export: "); print(key); print("="); print(val); print("\n");
            set_color(GFX_WHITE, GFX_BLACK);
        }
    }
    else if (str_compare(input_buffer, "paging")) {
        uint32_t cr0 = paging_get_cr0();
        char buf[20];
        if (cr0 & 0x80000000) {
            set_color(GFX_LGREEN, GFX_BLACK);
            print("Paging aktif");
        }
        else {
            set_color(GFX_LRED, GFX_BLACK);
            print("Paging tidak aktif");
        }
        print("\nCR0: 0x");
        itoa(cr0, buf);
        print(buf);
        print("\n");
        set_color(GFX_WHITE, GFX_BLACK);
    }
    else if(str_starts_with(input_buffer, "exec ")) {
        /* strip trailing '&' yang sudah di-parse di atas */
        const char *name = input_buffer + 5;
        uint32_t size;
        const uint8_t *data = fs_read_bin(name, &size);
        if (!data) {
            print("exec: file tidak ditemukan\n");
        } else {
            /* Buat page directory baru, isolasi penuh untuk proses ini */
            uint64_t *proc_dir = vmm_create_page_dir();
            uint64_t entry = elf_load(data, size, proc_dir);
            if (!entry) {
                print("exec: gagal memuat ELF\n");
            } else {
                uint64_t stack_phys = pmm_alloc_frame();
                vmm_map_page(proc_dir, 0x600000, stack_phys, 7);
                uint64_t user_esp = 0x600000 + PAGE_SIZE;
                int tid = task_create_user(entry, proc_dir, user_esp, name);
                if (bg_exec) {
                    char tbuf[8]; itoa((uint32_t)tid, tbuf);
                    print("exec: ["); print(tbuf); print("] "); print(name); print(" &\n");
                } else {
                    /* Foreground: blok shell sampai program selesai */
                    task_wait(tid);
                }
            }
        }
    }
    else if(str_compare(input_buffer, "ps")) {
        int i;
        int cur = task_get_current();
        set_color(GFX_YELLOW, GFX_BLACK);
        print("ID  PRIO  STATUS     CPU  NAMA\n");
        print("--- ----- ---------- ---- ----------------\n");
        set_color(GFX_WHITE, GFX_BLACK);
        for (i = 0; i < task_get_max(); i++) {
            if (!task_is_used(i)) continue;
            char buf[8];
            itoa(i, buf); print(buf); print("   ");
            int prio = task_get_priority(i);
            if (prio == 3)      set_color(GFX_LGREEN, GFX_BLACK);
            else if (prio == 2) set_color(GFX_YELLOW, GFX_BLACK);
            else                set_color(GFX_LGRAY, GFX_BLACK);
            itoa(prio, buf); print(buf); print("     ");
            int st = task_get_status(i);
            if (i == cur) {
                set_color(GFX_LGREEN, GFX_BLACK);
                print("running    ");
            } else if (st == TASK_SLEEPING) {
                set_color(GFX_LBLUE, GFX_BLACK);
                print("sleeping   ");
            } else if (st == TASK_BLOCKED) {
                set_color(GFX_LRED, GFX_BLACK);
                print("blocked    ");
            } else {
                set_color(GFX_LGRAY, GFX_BLACK);
                print("ready      ");
            }
            set_color(GFX_WHITE, GFX_BLACK);
            int cpu_id = task_get_cpu(i);
            if (cpu_id < 0) print("free ");
            else { itoa((uint32_t)cpu_id, buf); print("cpu"); print(buf); print(" "); }
            print(task_get_name(i));
            print("\n");
        }
    }
    else if(str_starts_with(input_buffer, "kill ")) {
        /* parse angka id dari "kill <id>" */
        const char *p = input_buffer + 5;
        int id = 0;
        while (*p >= '0' && *p <= '9') { id = id * 10 + (*p - '0'); p++; }
        if (id == 0) {
            set_color(GFX_LRED, GFX_BLACK);
            print("kill: tidak dapat mematikan shell (id 0)\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else if (task_kill(id)) {
            set_color(GFX_LGREEN, GFX_BLACK);
            print("kill: proses ");
            char buf[8]; itoa(id, buf); print(buf);
            print(" dihentikan\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else {
            set_color(GFX_LRED, GFX_BLACK);
            print("kill: proses tidak ditemukan atau tidak dapat dimatikan\n");
            set_color(GFX_WHITE, GFX_BLACK);
        }
    }
    else if(str_starts_with(input_buffer, "setprio ")) {
        /* parse: "setprio <id> <prio>" */
        const char *p = input_buffer + 8;
        int id = 0, prio = 0;
        while (*p >= '0' && *p <= '9') { id = id * 10 + (*p - '0'); p++; }
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { prio = prio * 10 + (*p - '0'); p++; }
        if (prio < 1 || prio > 3) {
            set_color(GFX_LRED, GFX_BLACK);
            print("setprio: priority harus 1, 2, atau 3\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else if (task_set_priority(id, prio)) {
            set_color(GFX_LGREEN, GFX_BLACK);
            print("setprio: proses ");
            char buf[8]; itoa(id, buf); print(buf);
            print(" priority diubah ke ");
            itoa(prio, buf); print(buf);
            print("\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else {
            set_color(GFX_LRED, GFX_BLACK);
            print("setprio: proses tidak ditemukan\n");
            set_color(GFX_WHITE, GFX_BLACK);
        }
    }
    else if(str_starts_with(input_buffer, "pipe ")) {
        /* Sintaks: pipe <prog1> <prog2> */
        const char *rest = input_buffer + 5;
        /* Pisahkan nama prog1 dan prog2 */
        char prog1[32], prog2[32];
        int j = 0;
        while (rest[j] && rest[j] != ' ' && j < 31) { prog1[j] = rest[j]; j++; }
        prog1[j] = '\0';
        if (rest[j] != ' ') {
            set_color(GFX_LRED, GFX_BLACK);
            print("pipe: gunakan: pipe <prog1> <prog2>\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else {
            const char *r2 = rest + j + 1;
            int k = 0;
            while (r2[k] && k < 31) { prog2[k] = r2[k]; k++; }
            prog2[k] = '\0';

            // Alokasi pipe baru
            int pipe_fd = pipe_alloc();
            if (pipe_fd < 0) {
                set_color(GFX_LRED, GFX_BLACK);
                print("pipe: gagal alokasi pipe (semua slot penuh)\n");
                set_color(GFX_WHITE, GFX_BLACK);
            } else {
                // Jalankan prog1 (writer)
                uint32_t sz1;
                const uint8_t *d1 = fs_read_bin(prog1, &sz1);
                // Jalankan prog2 (reader)
                uint32_t sz2;
                const uint8_t *d2 = fs_read_bin(prog2, &sz2);

                if (!d1 || !d2) {
                    pipe_free(pipe_fd);
                    set_color(GFX_LRED, GFX_BLACK);
                    if (!d1) { print("pipe: file tidak ditemukan: "); print(prog1); print("\n"); }
                    if (!d2) { print("pipe: file tidak ditemukan: "); print(prog2); print("\n"); }
                    set_color(GFX_WHITE, GFX_BLACK);
                } else {
                /* Buat dan jalankan prog1 (writer) */
                uint64_t *dir1 = vmm_create_page_dir();
                uint64_t entry1 = elf_load(d1, sz1, dir1);
                if (entry1) {
                    uint64_t sp1 = pmm_alloc_frame();
                    vmm_map_page(dir1, 0x600000, sp1, 7);
                    int tid1 = task_create_user(entry1, dir1, 0x600000 + PAGE_SIZE, prog1);
                    task_set_pipe(tid1, pipe_fd);
                }

                /* Buat dan jalankan prog2 (reader) */
                uint64_t *dir2 = vmm_create_page_dir();
                uint64_t entry2 = elf_load(d2, sz2, dir2);
                if (entry2) {
                    uint64_t sp2 = pmm_alloc_frame();
                    vmm_map_page(dir2, 0x600000, sp2, 7);
                    int tid2 = task_create_user(entry2, dir2, 0x600000 + PAGE_SIZE, prog2);
                    task_set_pipe(tid2, pipe_fd);
                }

                if (entry1 && entry2) {
                    set_color(GFX_LGREEN, GFX_BLACK);
                    print("pipe: ");
                    print(prog1);
                    print(" | ");
                    print(prog2);
                    print(" dimulai (pipe id=");
                    char pbuf[8]; itoa(pipe_fd, pbuf); print(pbuf);
                    print(")\n");
                    set_color(GFX_WHITE, GFX_BLACK);
                }
                }   /* end else (!d1 || !d2) */
            }       /* end else (pipe_fd >= 0) */
        }           /* end else (rest[j] == ' ') */
    }
    else if (str_compare(input_buffer, "ifconfig")) {
        net_ifconfig();
    }
    else if (str_compare(input_buffer, "ping")) {
        print("gunakan ping <ip>   contoh: ping 10.0.2.2\n");
    }
    else if (str_starts_with(input_buffer, "ping ")) {
        uint8_t ip[4];
        if (!parse_ip(input_buffer + 5, ip)) {
            set_color(GFX_LRED, GFX_BLACK);
            print("ping: alamat IP tidak valid\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else {
            net_ping(ip, 4);
        }
    }
    else if (str_compare(input_buffer, "cpuinfo")) {
        char nbuf[16];
        int i;

        print("cpu total: ");
        itoa((uint32_t)cpu_count, nbuf);
        print(nbuf);
        print("\n");

        print("ap online: ");
        itoa(smp_ap_started, nbuf);
        print(nbuf);
        print("\n");

        print("ap ticks:  ");
        itoa(smp_ap_ticks, nbuf);
        print(nbuf);
        print("\n");

        for (i = 0; i < cpu_count; i++) {
            print("cpu");
            itoa((uint32_t)i, nbuf);
            print(nbuf);
            print(": apic=");
            itoa((uint32_t)cpus[i].apic_id, nbuf);
            print(nbuf);
            print(" acpi=");
            itoa((uint32_t)cpus[i].acpi_id, nbuf);
            print(nbuf);
            print(i == 0 ? " (BSP)\n" : " (AP)\n");
        }
    }
    /* Tahap M: distribusi task per CPU */
    else if (str_compare(input_buffer, "taskstat")) {
        int i;
        char nbuf[8];
        int cnt[8] = {0,0,0,0,0,0,0,0};
        int free_cnt = 0;
        for (i = 0; i < task_get_max(); i++) {
            if (!task_is_used(i)) continue;
            int c = task_get_cpu(i);
            if (c < 0) free_cnt++;
            else if (c < 8) cnt[c]++;
        }
        set_color(GFX_YELLOW, GFX_BLACK);
        print("CPU  TASKS\n");
        print("---- -----\n");
        set_color(GFX_WHITE, GFX_BLACK);
        for (i = 0; i < cpu_count; i++) {
            print("cpu"); itoa((uint32_t)i, nbuf); print(nbuf); print("  ");
            itoa((uint32_t)cnt[i], nbuf); print(nbuf); print("\n");
        }
        print("free  "); itoa((uint32_t)free_cnt, nbuf); print(nbuf); print("\n");
    }
    /* Fondasi: meminfo — statistik PMM + kernel heap */
    else if (str_compare(input_buffer, "meminfo")) {
        char nbuf[16];
        uint32_t total = 16384u - 768u;   /* frame user: 768..16383 = 15616 frame */
        uint32_t free_f = pmm_free_count();
        uint32_t used_f = total - free_f;
        set_color(GFX_YELLOW, GFX_BLACK);
        print("=== Memory Info ===\n");
        set_color(GFX_WHITE, GFX_BLACK);
        print("Physical frames (4KB):\n");
        print("  Total : "); itoa(total, nbuf); print(nbuf); print(" frames (");
        itoa(total * 4u, nbuf); print(nbuf); print(" KB)\n");
        print("  Used  : "); itoa(used_f, nbuf); print(nbuf); print(" frames (");
        itoa(used_f * 4u, nbuf); print(nbuf); print(" KB)\n");
        print("  Free  : "); itoa(free_f, nbuf); print(nbuf); print(" frames (");
        itoa(free_f * 4u, nbuf); print(nbuf); print(" KB)\n");
        print("Kernel reserved: 0-3MB (768 frames)\n");
        print("Kernel heap    : 0x100000-0x6FFFFF (6MB)\n");
    }
    /* Tahap J: VFS commands */
    else if (str_starts_with(input_buffer, "open ")) {
        const char *path = input_buffer + 5;
        char nbuf[8];
        int fd = vfs_open(0, path, VFS_O_RDWR | VFS_O_CREATE);
        if (fd < 0) { set_color(GFX_LRED, GFX_BLACK); print("open: file tidak ditemukan\n"); }
        else { print("fd = "); itoa((uint32_t)fd, nbuf); print(nbuf); print("\n"); }
        set_color(GFX_WHITE, GFX_BLACK);
    }
    else if (str_starts_with(input_buffer, "fread ")) {
        const char *p = input_buffer + 6;
        int fd = 0;
        while (*p >= '0' && *p <= '9') { fd = fd * 10 + (*p - '0'); p++; }
        char buf[128];
        int n = vfs_read(0, fd, buf, 127);
        if (n <= 0) { set_color(GFX_LGRAY, GFX_BLACK); print("(kosong atau EOF)\n"); }
        else { buf[n] = '\0'; print(buf); print("\n"); }
        set_color(GFX_WHITE, GFX_BLACK);
    }
    else if (str_starts_with(input_buffer, "fwrite ")) {
        /* fwrite <fd> <teks> */
        const char *p = input_buffer + 7;
        int fd = 0;
        while (*p >= '0' && *p <= '9') { fd = fd * 10 + (*p - '0'); p++; }
        if (*p == ' ') p++;
        int n = vfs_write(0, fd, p, str_len(p));
        char nbuf[8]; itoa((uint32_t)n, nbuf);
        print("tulis "); print(nbuf); print(" byte\n");
    }
    else if (str_starts_with(input_buffer, "fclose ")) {
        const char *p = input_buffer + 7;
        int fd = 0;
        while (*p >= '0' && *p <= '9') { fd = fd * 10 + (*p - '0'); p++; }
        if (vfs_close(0, fd) == 0) print("fd ditutup\n");
        else { set_color(GFX_LRED, GFX_BLACK); print("fclose: fd tidak valid\n"); }
        set_color(GFX_WHITE, GFX_BLACK);
    }
    /* Tahap L: Message Queue commands */
    else if (str_starts_with(input_buffer, "mq_send ")) {
        /* mq_send <pid> <msg> */
        const char *p = input_buffer + 8;
        int pid = 0;
        while (*p >= '0' && *p <= '9') { pid = pid * 10 + (*p - '0'); p++; }
        if (*p == ' ') p++;
        int n = str_len(p);
        if (n > MQ_MSG_SIZE) n = MQ_MSG_SIZE;
        int r = mq_send(pid, (const uint8_t*)p, n, 0);
        if (r == 0) { set_color(GFX_LGREEN, GFX_BLACK); print("pesan terkirim\n"); }
        else { set_color(GFX_LRED, GFX_BLACK); print("mq_send: gagal\n"); }
        set_color(GFX_WHITE, GFX_BLACK);
    }
    else if (str_compare(input_buffer, "mq_recv")) {
        uint8_t buf[MQ_MSG_SIZE + 1];
        int from = -1;
        int n = mq_recv(0, buf, MQ_MSG_SIZE, &from);
        if (n <= 0) { set_color(GFX_LGRAY, GFX_BLACK); print("(tidak ada pesan)\n"); }
        else {
            char nbuf[8];
            buf[n] = '\0';
            set_color(GFX_LGREEN, GFX_BLACK);
            print("dari pid "); itoa((uint32_t)from, nbuf); print(nbuf); print(": ");
            print((const char*)buf); print("\n");
        }
        set_color(GFX_WHITE, GFX_BLACK);
    }
    else {
        /* Cek apakah ada operator ' | ' (pipe inline) */
        int pipe_pos = -1;
        int slen = str_len(input_buffer);
        int pi;
        for (pi = 1; pi < slen - 2; pi++) {
            if (input_buffer[pi]   == ' ' &&
                input_buffer[pi+1] == '|' &&
                input_buffer[pi+2] == ' ') {
                pipe_pos = pi;
                break;
            }
        }

        if (pipe_pos > 0) {
            /* Sintaks: prog1 | prog2 */
            char prog1[32], prog2[32];
            int j;
            for (j = 0; j < pipe_pos && j < 31; j++) prog1[j] = input_buffer[j];
            prog1[j] = '\0';
            const char *r2 = input_buffer + pipe_pos + 3;
            for (j = 0; r2[j] && j < 31; j++) prog2[j] = r2[j];
            prog2[j] = '\0';

            int pipe_fd = pipe_alloc();
            if (pipe_fd < 0) {
                set_color(GFX_LRED, GFX_BLACK);
                print("pipe: gagal alokasi pipe (slot penuh)\n");
                set_color(GFX_WHITE, GFX_BLACK);
            } else {
                uint32_t sz1, sz2;
                const uint8_t *d1 = fs_read_bin(prog1, &sz1);
                const uint8_t *d2 = fs_read_bin(prog2, &sz2);
                if (!d1 || !d2) {
                    pipe_free(pipe_fd);
                    set_color(GFX_LRED, GFX_BLACK);
                    if (!d1) { print("pipe: file tidak ditemukan: "); print(prog1); print("\n"); }
                    if (!d2) { print("pipe: file tidak ditemukan: "); print(prog2); print("\n"); }
                    set_color(GFX_WHITE, GFX_BLACK);
                } else {
                    uint64_t *dir1 = vmm_create_page_dir();
                    uint64_t entry1 = elf_load(d1, sz1, dir1);
                    if (entry1) {
                        uint64_t sp1 = pmm_alloc_frame();
                        vmm_map_page(dir1, 0x600000, sp1, 7);
                        int tid1 = task_create_user(entry1, dir1, 0x600000 + PAGE_SIZE, prog1);
                        task_set_pipe(tid1, pipe_fd);
                    }
                    uint64_t *dir2 = vmm_create_page_dir();
                    uint64_t entry2 = elf_load(d2, sz2, dir2);
                    if (entry2) {
                        uint64_t sp2 = pmm_alloc_frame();
                        vmm_map_page(dir2, 0x600000, sp2, 7);
                        int tid2 = task_create_user(entry2, dir2, 0x600000 + PAGE_SIZE, prog2);
                        task_set_pipe(tid2, pipe_fd);
                    }
                    if (entry1 && entry2) {
                        set_color(GFX_LGREEN, GFX_BLACK);
                        print(prog1); print(" | "); print(prog2);
                        print(" dimulai (pipe id=");
                        char pbuf[8]; itoa(pipe_fd, pbuf); print(pbuf);
                        print(")\n");
                        set_color(GFX_WHITE, GFX_BLACK);
                    }
                }
            }
        } else {
            print(input_buffer);
            print("\n");
            set_color(GFX_WHITE, GFX_BLACK);
        }
    }
}

void shell_init() {
    set_color(GFX_YELLOW, GFX_BLACK);
    print("\nKetik 'help' untuk daftar perintah\n");
    set_color(GFX_LGREEN, GFX_BLACK);
    print("> ");
    set_color(GFX_WHITE, GFX_BLACK);
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
            hist_cursor = -1;  /* reset browse position */
        }

        input_len = 0;
        shell_execute();
        input_len = 0;          /* reset ulang: shell_expand_vars() mungkin menulisnya */
        input_buffer[0] = '\0';
        set_color(GFX_LGREEN, GFX_BLACK);
        print("> ");
        set_color(GFX_WHITE, GFX_BLACK);
    }
    else if(c== '\b'){                      /* backspace */
        if (input_len > 0) {
            input_len--;
            backspace_char();
        }
    }
    else if (c == '\x01') {                 /* ↑ up: maju ke history lebih lama */
        if (hist_count == 0) return;
        if (hist_cursor == -1) hist_cursor = 0;
        else if (hist_cursor < hist_count - 1) hist_cursor++;
        else return;
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
    else if (c == '\x02') {                 /* ↓ down: kembali ke history lebih baru */
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
    else if (c == '\x03') {                 /* Tab: auto-complete */
        shell_tab_complete();
    }
    else {
        if (input_len < 255) {
            hist_cursor = -1;  /* keluar dari mode browse saat mengetik */
            input_buffer[input_len] = c;
            input_len++;
            print_char(c);
        }
    }
}