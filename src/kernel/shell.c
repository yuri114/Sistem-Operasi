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
#include "websocket.h"
#include "virtio_blk.h"
#include "acpi.h"
#include "smp.h"
#include "vfs.h"
#include "mq.h"
#include "keyboard.h"
#include "mfs4.h"
#include "tls13.h"
#include "x509.h"
#include "ext2.h"
#include "serial.h"
#include "rtc.h"

/*fungsi dari kernel.c*/
void print(const char *str);
void print_char(char c);
void clear_screen();
void backspace_char();
void itoa(uint32_t num, char *buf);
void set_color(uint32_t fg, uint32_t bg);

/* GUI mode — dari kernel.c / window.c */
extern int  g_gui_mode;
extern void wm_init(void);
extern void mouse_init(void);
extern void wp_blit(void);
extern int  wp_is_loaded(void);

/* AT4 — Clipboard (dari syscall.c) */
extern void clip_copy(const char *s);
extern int  clip_paste(char *dst, int maxlen);

/* Buffer untuk menyimpan input dari keyboard */
static char input_buffer[256];
static int input_len = 0;

/* Exit code dari perintah terakhir — digunakan oleh operator && / || */
static int last_exit_code = 0;

/* History ring buffer */
#define HISTORY_SIZE 32
static char history[HISTORY_SIZE][256];
static int hist_head  = 0;  // slot berikutnya untuk ditulis
static int hist_count = 0;  // jumlah entri tersimpan (maks HISTORY_SIZE)
static int hist_cursor = -1; // -1 = tidak browse; 0 = paling baru, 1 = sebelumnya

/* ================================================================
 * FONDASI AE — Text Editor State
 * ================================================================ */
#define ED_VIEW_ROWS  53    /* baris konten: row 2..54 (VGA_ROWS=60) */
#define ED_MAX_LINES 200    /* maks baris per file                   */
#define ED_LINE_CAP  160    /* maks karakter per baris (VGA_COLS)    */
static int  editor_active = 0;
static char ed_lines[ED_MAX_LINES][ED_LINE_CAP + 1];
static int  ed_len[ED_MAX_LINES];
static int  ed_nlines    = 0;
static int  ed_cur_row   = 0;    /* posisi kursor di dokumen */
static int  ed_cur_col   = 0;
static int  ed_scroll    = 0;    /* baris pertama yang tampil */
static char ed_fname[64];
static int  ed_modified  = 0;   /* 0=bersih, 1=ada ubahan, -1=diperingatkan */
static char ed_smsg[80];        /* pesan status sementara */
static char ed_outbuf[ED_MAX_LINES * (ED_LINE_CAP + 2)]; /* buffer simpan */

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
    "help", "clear", "about", "memtest", "uptime", "date",
    "time", "reboot", "shutdown", "poweroff", "ls", "paging", "ps",
    "echo ", "exec ", "read ", "write ", "del ", "rename ", "kill ",
    "cd ", "pwd", "export ", "env",
    "sync", "mkdir ", "chmod ",
    "ifconfig", "ping ", "cpuinfo",
    "udp_send ", "tcp_get ", "nslookup ", "curl ", "ntpdate", "httpd ",
    "open ", "fread ", "fwrite ", "fclose ",
    "mq_send ", "mq_recv", "taskstat", "meminfo", "threadtest", "futextest",
    "polltest",
    "cat ", "wc ", "head ", "cp ", "mv ", "edit ", "grep ",
    "sh ", "history", "ps", "uptime",
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
            /* $? = last exit code */
            if (input_buffer[ii] == '?') {
                ii++;
                char ecbuf[8]; itoa((uint32_t)(last_exit_code < 0 ? (uint32_t)(-last_exit_code) : (uint32_t)last_exit_code), ecbuf);
                const char *ep = ecbuf;
                if (last_exit_code < 0 && ti < 253) tmp[ti++] = '-';
                while (*ep && ti < 254) tmp[ti++] = *ep++;
                continue;
            }
            /* $$ = PID of current task */
            if (input_buffer[ii] == '$') {
                ii++;
                char pidbuf[8]; itoa((uint32_t)task_get_current(), pidbuf);
                const char *pp = pidbuf;
                while (*pp && ti < 254) tmp[ti++] = *pp++;
                continue;
            }
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

/* ================================================================
 * FONDASI AE — Text Editor Helper Functions
 * ================================================================ */

/* Pindahkan kursor ANSI ke baris/kolom layar (1-based) */
static void ed_goto(int row, int col) {
    char buf[20]; int i = 0;
    int r = row > 0 ? row : 1;
    int c = col  > 0 ? col  : 1;
    buf[i++] = '\033'; buf[i++] = '[';
    { char rev[8]; int ri = 0, n = r;
      while (n > 0) { rev[ri++] = (char)('0' + n % 10); n /= 10; }
      while (ri > 0) buf[i++] = rev[--ri]; }
    buf[i++] = ';';
    { char rev[8]; int ri = 0, n = c;
      while (n > 0) { rev[ri++] = (char)('0' + n % 10); n /= 10; }
      while (ri > 0) buf[i++] = rev[--ri]; }
    buf[i++] = 'H'; buf[i] = '\0';
    print(buf);
}

static void ed_putn(uint32_t n) { char b[12]; itoa(n, b); print(b); }

/* Gambar satu baris dokumen di layar */
static void ed_draw_row(int doc_row) {
    int scr_row = doc_row - ed_scroll + 2;
    if (scr_row < 2 || scr_row > ED_VIEW_ROWS + 1) return;
    ed_goto(scr_row, 1);
    { int ln = doc_row + 1;
      set_color(GFX_DGRAY, GFX_BLACK);
      if (ln < 10)   print("   ");
      else if (ln < 100)  print("  ");
      else if (ln < 1000) print(" ");
      ed_putn((uint32_t)ln); print(" "); }
    if (doc_row == ed_cur_row) set_color(GFX_WHITE, GFX_BLACK);
    else                       set_color(GFX_LGRAY, GFX_BLACK);
    ed_lines[doc_row][ed_len[doc_row]] = '\0';
    print(ed_lines[doc_row]);
    print("\033[K");
    set_color(GFX_WHITE, GFX_BLACK);
}

/* Gambar status bar di baris 90 */
static void ed_draw_status(void) {
    ed_goto(90, 1);
    set_color(GFX_BLACK, GFX_YELLOW);
    print(" Baris:"); ed_putn((uint32_t)(ed_cur_row + 1));
    print("/");       ed_putn((uint32_t)ed_nlines);
    print("  Kol:");  ed_putn((uint32_t)(ed_cur_col + 1));
    print("  ");
    if (ed_smsg[0]) print(ed_smsg);
    print("\033[K");
    set_color(GFX_WHITE, GFX_BLACK);
}

/* Pastikan scroll mencakup kursor, lalu posisikan kursor ANSI */
static void ed_fix_cursor(void) {
    if (ed_cur_row < ed_scroll) ed_scroll = ed_cur_row;
    if (ed_cur_row >= ed_scroll + ED_VIEW_ROWS)
        ed_scroll = ed_cur_row - ED_VIEW_ROWS + 1;
    ed_goto((ed_cur_row - ed_scroll) + 2, ed_cur_col + 6);
}

/* Gambar ulang seluruh layar editor */
static void ed_render_all(void) {
    int i;
    print("\033[2J");
    /* Header */
    ed_goto(1, 1);
    set_color(GFX_BLACK, GFX_LCYAN);
    print(" EDIT: "); print(ed_fname);
    if (ed_modified) print(" [*]");
    print("   Ctrl+S=Simpan  Ctrl+Q=Keluar  Del=Hapus");
    print("\033[K");
    set_color(GFX_WHITE, GFX_BLACK);
    /* Baris konten */
    for (i = 0; i < ED_VIEW_ROWS; i++) {
        int doc_row = ed_scroll + i;
        ed_goto(i + 2, 1);
        if (doc_row < ed_nlines) {
            int ln = doc_row + 1;
            set_color(GFX_DGRAY, GFX_BLACK);
            if (ln < 10)   print("   ");
            else if (ln < 100)  print("  ");
            else if (ln < 1000) print(" ");
            ed_putn((uint32_t)ln); print(" ");
            if (doc_row == ed_cur_row) set_color(GFX_WHITE, GFX_BLACK);
            else                       set_color(GFX_LGRAY, GFX_BLACK);
            ed_lines[doc_row][ed_len[doc_row]] = '\0';
            print(ed_lines[doc_row]);
        }
        print("\033[K");
        set_color(GFX_WHITE, GFX_BLACK);
    }
    ed_draw_status();
    ed_fix_cursor();
}

/* Muat file ke ed_lines (buat baru jika tidak ada) */
static void ed_load(const char *fname) {
    int i;
    for (i = 0; i < ED_MAX_LINES; i++) { ed_len[i] = 0; ed_lines[i][0] = '\0'; }
    ed_nlines = 0; ed_cur_row = 0; ed_cur_col = 0; ed_scroll = 0;
    ed_modified = 0; ed_smsg[0] = '\0';
    i = 0; while (fname[i] && i < 63) { ed_fname[i] = fname[i]; i++; }
    ed_fname[i] = '\0';
    { uint32_t fsz;
      const uint8_t *fdata = fs_read_bin(fname, &fsz);
      if (!fdata || fsz == 0) { ed_nlines = 1; return; }
      { uint32_t di = 0; int col = 0;
        while (di < fsz && ed_nlines < ED_MAX_LINES) {
            char ch = (char)fdata[di++];
            if (ch == '\r') continue;
            if (ch == '\n') {
                ed_lines[ed_nlines][col] = '\0';
                ed_len[ed_nlines] = col;
                ed_nlines++; col = 0;
            } else if (col < ED_LINE_CAP) {
                ed_lines[ed_nlines][col++] = ch;
            }
        }
        if (col > 0 || ed_nlines == 0) {
            ed_lines[ed_nlines][col] = '\0';
            ed_len[ed_nlines] = col;
            ed_nlines++;
        }
      }
    }
    if (ed_nlines == 0) ed_nlines = 1;
}

/* Simpan ed_lines ke file */
static void ed_save(void) {
    int oi = 0, i, j;
    const char *msg;
    for (i = 0; i < ed_nlines; i++) {
        for (j = 0; j < ed_len[i] && oi < (int)sizeof(ed_outbuf) - 2; j++)
            ed_outbuf[oi++] = ed_lines[i][j];
        if (i < ed_nlines - 1) ed_outbuf[oi++] = '\n';
    }
    ed_outbuf[oi] = '\0';
    if (fs_write(ed_fname, ed_outbuf)) {
        ed_modified = 0; msg = "Tersimpan!";
    } else {
        msg = "Gagal menyimpan!";
    }
    i = 0; while (msg[i]) ed_smsg[i] = msg[i++]; ed_smsg[i] = '\0';
}

/* Proses keystroke saat editor aktif */
static void ed_process_char(char c) {
    int i, j;
    ed_smsg[0] = '\0';

    /* Ctrl+Q = keluar */
    if (c == '\x11') {
        if (ed_modified > 0) {
            const char *msg = "Ada perubahan! Ctrl+Q lagi = keluar tanpa simpan";
            i = 0; while (msg[i]) ed_smsg[i] = msg[i++]; ed_smsg[i] = '\0';
            ed_modified = -1;
            ed_draw_status(); ed_fix_cursor();
        } else {
            editor_active = 0;
            print("\033[2J"); ed_goto(1, 1);
            set_color(GFX_WHITE, GFX_BLACK);
            set_color(GFX_LGREEN, GFX_BLACK); print("> ");
            set_color(GFX_WHITE, GFX_BLACK);
        }
        return;
    }
    /* Reset status peringatan jika tombol lain ditekan */
    if (ed_modified == -1) ed_modified = 1;

    /* Ctrl+S = simpan */
    if (c == '\x13') { ed_save(); ed_draw_status(); ed_fix_cursor(); return; }

    /* AT4 — Ctrl+V: tempel clipboard ke posisi kursor editor */
    if (c == '\x16') {
        char pbuf[256];
        int plen = clip_paste(pbuf, 256);
        int pi;
        for (pi = 0; pi < plen; pi++) {
            char pc = pbuf[pi];
            if (pc == '\n') {
                /* sisipkan baris baru jika ada newline di clipboard */
                if (ed_nlines < ED_MAX_LINES) {
                    int row = ed_cur_row, col = ed_cur_col;
                    int ln = ed_len[row];
                    int ni = ed_nlines - 1;
                    /* geser baris ke bawah */
                    for (; ni > row; ni--) {
                        ed_len[ni+1] = ed_len[ni];
                        int k2;
                        for (k2 = 0; k2 <= ed_len[ni]; k2++)
                            ed_lines[ni+1][k2] = ed_lines[ni][k2];
                    }
                    ed_nlines++;
                    /* baris baru = sisa baris saat ini */
                    int suf = ln - col;
                    int k3;
                    for (k3 = 0; k3 < suf; k3++)
                        ed_lines[row+1][k3] = ed_lines[row][col+k3];
                    ed_len[row+1] = suf;
                    ed_lines[row+1][suf] = '\0';
                    ed_len[row] = col;
                    ed_lines[row][col] = '\0';
                    ed_cur_row++; ed_cur_col = 0;
                    ed_modified = 1;
                }
            } else if (pc >= ' ' && ed_cur_col < ED_LINE_CAP - 1) {
                /* sisipkan karakter biasa */
                int row = ed_cur_row, col = ed_cur_col;
                int ln = ed_len[row];
                int k4;
                for (k4 = ln; k4 > col; k4--)
                    ed_lines[row][k4] = ed_lines[row][k4-1];
                ed_lines[row][col] = pc;
                ed_len[row]++;
                ed_lines[row][ed_len[row]] = '\0';
                ed_cur_col++;
                ed_modified = 1;
            }
        }
        ed_render_all(); ed_draw_status(); ed_fix_cursor(); return;
    }

    /* Navigasi atas */
    if (c == '\x01') {
        if (ed_cur_row > 0) {
            ed_cur_row--;
            if (ed_cur_col > ed_len[ed_cur_row]) ed_cur_col = ed_len[ed_cur_row];
        }
        if (ed_cur_row < ed_scroll) { ed_scroll = ed_cur_row; ed_render_all(); return; }
        ed_draw_row(ed_cur_row); ed_draw_row(ed_cur_row + 1);
        ed_draw_status(); ed_fix_cursor(); return;
    }
    /* Navigasi bawah */
    if (c == '\x02') {
        if (ed_cur_row < ed_nlines - 1) {
            ed_cur_row++;
            if (ed_cur_col > ed_len[ed_cur_row]) ed_cur_col = ed_len[ed_cur_row];
        }
        if (ed_cur_row >= ed_scroll + ED_VIEW_ROWS) {
            ed_scroll = ed_cur_row - ED_VIEW_ROWS + 1;
            ed_render_all(); return;
        }
        ed_draw_row(ed_cur_row - 1); ed_draw_row(ed_cur_row);
        ed_draw_status(); ed_fix_cursor(); return;
    }
    /* Navigasi kiri */
    if (c == '\x04') {
        if (ed_cur_col > 0) {
            ed_cur_col--;
        } else if (ed_cur_row > 0) {
            ed_cur_row--; ed_cur_col = ed_len[ed_cur_row];
            if (ed_cur_row < ed_scroll) { ed_scroll = ed_cur_row; ed_render_all(); return; }
        }
        ed_draw_status(); ed_fix_cursor(); return;
    }
    /* Navigasi kanan */
    if (c == '\x05') {
        if (ed_cur_col < ed_len[ed_cur_row]) {
            ed_cur_col++;
        } else if (ed_cur_row < ed_nlines - 1) {
            ed_cur_row++; ed_cur_col = 0;
            if (ed_cur_row >= ed_scroll + ED_VIEW_ROWS) {
                ed_scroll = ed_cur_row - ED_VIEW_ROWS + 1;
                ed_render_all(); return;
            }
        }
        ed_draw_status(); ed_fix_cursor(); return;
    }
    /* Delete (KEY_DELETE) = hapus karakter di kursor */
    if (c == '\x06') {
        if (ed_cur_col < ed_len[ed_cur_row]) {
            int ln = ed_len[ed_cur_row];
            for (j = ed_cur_col; j < ln - 1; j++)
                ed_lines[ed_cur_row][j] = ed_lines[ed_cur_row][j+1];
            ed_lines[ed_cur_row][ln-1] = '\0';
            ed_len[ed_cur_row]--; ed_modified = 1;
        } else if (ed_cur_row < ed_nlines - 1) {
            int cur_len = ed_len[ed_cur_row];
            int nxt_len = ed_len[ed_cur_row + 1];
            if (cur_len + nxt_len <= ED_LINE_CAP) {
                for (j = 0; j < nxt_len; j++)
                    ed_lines[ed_cur_row][cur_len + j] = ed_lines[ed_cur_row+1][j];
                ed_len[ed_cur_row] = cur_len + nxt_len;
                ed_lines[ed_cur_row][ed_len[ed_cur_row]] = '\0';
                for (i = ed_cur_row + 1; i < ed_nlines - 1; i++) {
                    int k; ed_len[i] = ed_len[i+1];
                    for (k = 0; k <= ed_len[i]; k++) ed_lines[i][k] = ed_lines[i+1][k];
                }
                ed_nlines--; ed_modified = 1; ed_render_all(); return;
            }
        }
        ed_draw_row(ed_cur_row); ed_draw_status(); ed_fix_cursor(); return;
    }
    /* Backspace = hapus karakter sebelum kursor */
    if (c == '\b') {
        if (ed_cur_col > 0) {
            int ln = ed_len[ed_cur_row];
            for (j = ed_cur_col - 1; j < ln - 1; j++)
                ed_lines[ed_cur_row][j] = ed_lines[ed_cur_row][j+1];
            ed_lines[ed_cur_row][ln-1] = '\0';
            ed_len[ed_cur_row]--; ed_cur_col--; ed_modified = 1;
        } else if (ed_cur_row > 0) {
            int prev_len = ed_len[ed_cur_row - 1];
            int cur_len  = ed_len[ed_cur_row];
            if (prev_len + cur_len <= ED_LINE_CAP) {
                for (j = 0; j < cur_len; j++)
                    ed_lines[ed_cur_row-1][prev_len+j] = ed_lines[ed_cur_row][j];
                ed_len[ed_cur_row-1] = prev_len + cur_len;
                ed_lines[ed_cur_row-1][ed_len[ed_cur_row-1]] = '\0';
                for (i = ed_cur_row; i < ed_nlines - 1; i++) {
                    int k; ed_len[i] = ed_len[i+1];
                    for (k = 0; k <= ed_len[i]; k++) ed_lines[i][k] = ed_lines[i+1][k];
                }
                ed_nlines--; ed_cur_row--; ed_cur_col = prev_len; ed_modified = 1;
                if (ed_cur_row < ed_scroll) ed_scroll = ed_cur_row;
                ed_render_all(); return;
            }
        }
        ed_draw_row(ed_cur_row); ed_draw_status(); ed_fix_cursor(); return;
    }
    /* Enter = sisipkan baris baru */
    if (c == '\n') {
        if (ed_nlines < ED_MAX_LINES) {
            int ln = ed_len[ed_cur_row];
            char rem[ED_LINE_CAP + 1]; int rlen = ln - ed_cur_col;
            for (j = 0; j < rlen; j++) rem[j] = ed_lines[ed_cur_row][ed_cur_col + j];
            rem[rlen] = '\0';
            ed_lines[ed_cur_row][ed_cur_col] = '\0';
            ed_len[ed_cur_row] = ed_cur_col;
            for (i = ed_nlines; i > ed_cur_row + 1; i--) {
                int k; ed_len[i] = ed_len[i-1];
                for (k = 0; k <= ed_len[i]; k++) ed_lines[i][k] = ed_lines[i-1][k];
            }
            ed_cur_row++;
            for (j = 0; j <= rlen; j++) ed_lines[ed_cur_row][j] = rem[j];
            ed_len[ed_cur_row] = rlen; ed_cur_col = 0;
            ed_nlines++; ed_modified = 1;
            if (ed_cur_row >= ed_scroll + ED_VIEW_ROWS) ed_scroll++;
            ed_render_all();
        }
        return;
    }
    /* Karakter biasa: sisipkan di posisi kursor */
    if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F) {
        int ln = ed_len[ed_cur_row];
        if (ln < ED_LINE_CAP) {
            for (j = ln; j > ed_cur_col; j--)
                ed_lines[ed_cur_row][j] = ed_lines[ed_cur_row][j-1];
            ed_lines[ed_cur_row][ed_cur_col] = c;
            ed_len[ed_cur_row]++; ed_cur_col++; ed_modified = 1;
        }
        ed_draw_row(ed_cur_row); ed_draw_status(); ed_fix_cursor();
    }
}

/* ================================================================
 * Fondasi AC — Shell Scripting Engine
 * Mendukung: #komentar, VAR=value, if/then/else/fi, for/do/done
 * ================================================================ */
static void shell_execute(void);  /* forward declaration */
#define SC_MAX_LINES   128
#define SC_LINE_LEN    200
static char  sc_lines[SC_MAX_LINES][SC_LINE_LEN];
static int   sc_nlines = 0;
static int   sc_argc   = 0;
static char  sc_arg_buf[512];
static char *sc_argv[10];

/* Helper: set shell env variable */
static void sc_setvar(const char *key, const char *val) {
    int i;
    for (i = 0; i < env_count; i++) {
        if (str_compare(env_keys[i], key)) {
            int j = 0;
            while (val[j] && j < 63) env_vals[i][j] = val[j], j++;
            env_vals[i][j] = '\0';
            return;
        }
    }
    if (env_count < ENV_MAX) {
        int j = 0;
        while (key[j] && j < 23) env_keys[env_count][j] = key[j], j++;
        env_keys[env_count][j] = '\0';
        j = 0;
        while (val[j] && j < 63) env_vals[env_count][j] = val[j], j++;
        env_vals[env_count][j] = '\0';
        env_count++;
    }
}

/* Helper: get env variable value, return "" if not found */
static const char *sc_getvar(const char *key) {
    int i;
    for (i = 0; i < env_count; i++)
        if (str_compare(env_keys[i], key)) return env_vals[i];
    return "";
}

/* Evaluate test expression:  -f FILE | -z VAR | A = B | A != B  */
static int sc_eval_test(const char *expr) {
    const char *p = expr;
    while (*p == ' ') p++;
    if (p[0] == '-' && p[1] == 'f' && (p[2]==' '||p[2]=='\0')) {
        /* -f FILE: file exists? */
        const char *fn = p + 2; while (*fn == ' ') fn++;
        uint32_t sz; return (fs_read_bin(fn, &sz) != 0) ? 1 : 0;
    }
    if (p[0] == '-' && p[1] == 'z') {
        /* -z VAR: empty? */
        const char *vn = p + 2; while (*vn == ' ') vn++;
        const char *v = (*vn=='$') ? sc_getvar(vn+1) : vn;
        return (v[0] == '\0') ? 1 : 0;
    }
    if (p[0] == '-' && p[1] == 'n') {
        /* -n VAR: non-empty? */
        const char *vn = p + 2; while (*vn == ' ') vn++;
        const char *v = (*vn=='$') ? sc_getvar(vn+1) : vn;
        return (v[0] != '\0') ? 1 : 0;
    }
    /* A = B or A != B or A == B */
    char lhs[64]; int li = 0;
    while (*p && *p != ' ' && li < 63) lhs[li++] = *p++;
    lhs[li] = '\0';
    while (*p == ' ') p++;
    char op[4]; int oi = 0;
    while (*p && *p != ' ' && oi < 3) op[oi++] = *p++;
    op[oi] = '\0';
    while (*p == ' ') p++;
    char rhs[64]; int ri = 0;
    /* strip quotes */
    if (*p == '"' || *p == '\'') p++;
    while (*p && *p != '"' && *p != '\'' && ri < 63) rhs[ri++] = *p++;
    rhs[ri] = '\0';
    /* expand $VAR */
    const char *lv = (lhs[0]=='$') ? sc_getvar(lhs+1) : lhs;
    const char *rv = (rhs[0]=='$') ? sc_getvar(rhs+1) : rhs;
    int eq = str_compare(lv, rv);
    if (op[0]=='=' || (op[0]=='='&&op[1]=='=')) return eq;
    if (op[0]=='!'&&op[1]=='=')                 return !eq;
    return 0;
}

/* Evaluate condition line: "[ EXPR ]" → 1=true, 0=false */
static int sc_eval_cond(const char *line) {
    const char *p = line;
    while (*p == ' ') p++;
    if (*p == '[') {
        p++;
        while (*p == ' ') p++;
        /* find closing ] */
        char expr[128]; int ei = 0;
        while (*p && *p != ']' && ei < 127) { expr[ei++] = *p++; }
        /* strip trailing space */
        while (ei > 0 && expr[ei-1] == ' ') ei--;
        expr[ei] = '\0';
        return sc_eval_test(expr);
    }
    return 0;
}

/* Execute one script line (sets input_buffer, calls shell_execute)
 * Returns 1 if line was a VAR=value assignment, 0 otherwise */
static int sc_exec_line(const char *line);  /* forward decl */

/* Find matching done/fi in sc_lines starting from `from`, return index or -1 */
static int sc_find_end(int from, const char *end_kw) {
    int depth = 0, i;
    for (i = from; i < sc_nlines; i++) {
        const char *l = sc_lines[i];
        while (*l == ' ') l++;
        if (str_starts_with(l, "if ") || str_starts_with(l, "while ") || str_starts_with(l, "for "))
            depth++;
        if (str_compare(l, end_kw)) {
            if (depth == 0) return i;
            depth--;
        }
    }
    return -1;
}

/* Execute script lines [from..to), return index of next line to execute  */
static int sc_exec_block(int from, int to);

static int sc_exec_block(int from, int to) {
    int i = from;
    while (i < to) {
        const char *raw = sc_lines[i];
        const char *line = raw;
        while (*line == ' ') line++;
        /* skip empty and comments */
        if (!*line || *line == '#') { i++; continue; }

        /* if COND; then / if COND */
        if (str_starts_with(line, "if ")) {
            /* extract condition (strip "; then" or "then" if present) */
            const char *cond = line + 3;
            while (*cond == ' ') cond++;
            char cbuf[128]; int ci2 = 0;
            while (cond[ci2] && cond[ci2] != ';' && ci2 < 127) cbuf[ci2] = cond[ci2], ci2++;
            /* strip " then" from end */
            while (ci2>0 && cbuf[ci2-1]==' ') ci2--;
            while (ci2>5 && cbuf[ci2-4]==' '&&cbuf[ci2-3]=='t'&&cbuf[ci2-2]=='h'&&cbuf[ci2-1]=='e'&&cbuf[ci2]=='n') ci2-=5;
            cbuf[ci2] = '\0';
            int cond_result = sc_eval_cond(cbuf);
            /* find then / else / fi */
            int then_start = i + 1;
            /* skip "then" line if it's separate */
            if (then_start < to) {
                const char *tl = sc_lines[then_start];
                while (*tl == ' ') tl++;
                if (str_compare(tl, "then")) then_start++;
            }
            int else_line = -1, fi_line = -1;
            {
                int depth2 = 0, j2;
                for (j2 = then_start; j2 < to; j2++) {
                    const char *l2 = sc_lines[j2]; while (*l2==' ') l2++;
                    if (str_starts_with(l2,"if ")||str_starts_with(l2,"while ")||str_starts_with(l2,"for ")) depth2++;
                    if (depth2==0 && str_compare(l2,"else")) { else_line = j2; }
                    if (str_compare(l2,"fi")) { if (depth2==0){fi_line=j2; break;} depth2--; }
                }
            }
            if (fi_line < 0) fi_line = to;
            if (cond_result) {
                int block_end = (else_line >= 0) ? else_line : fi_line;
                i = sc_exec_block(then_start, block_end);
            } else if (else_line >= 0) {
                i = sc_exec_block(else_line + 1, fi_line);
            }
            i = fi_line + 1;
            continue;
        }

        /* for VAR in WORD...; do */
        if (str_starts_with(line, "for ")) {
            char fbuf[128]; int fi2 = 0;
            const char *fp = line + 4;
            while (fp[fi2] && fp[fi2] != ';' && fi2 < 127) fbuf[fi2] = fp[fi2], fi2++;
            fbuf[fi2] = '\0';
            /* fbuf = "VAR in W1 W2 W3" */
            char *fvar = fbuf; while (*fvar == ' ') fvar++;
            char *fin = fvar; while (*fin && *fin != ' ') fin++;
            *fin++ = '\0'; while (*fin == ' ') fin++;
            /* skip "in " */
            if (fin[0]=='i'&&fin[1]=='n'&&fin[2]==' ') fin += 3;
            /* find "done" */
            int done_line = sc_find_end(i+1, "done");
            /* skip "do" line if present */
            int do_start = i + 1;
            {
                const char *dl = sc_lines[do_start]; while (*dl==' ') dl++;
                if (str_compare(dl,"do")) do_start++;
            }
            if (done_line < 0) done_line = to;
            /* iterate over words */
            char wbuf[96]; int wi = 0;
            while (*fin) {
                if (*fin == ' ' || *fin == '\0' || fin[0]=='\0') {
                    if (wi > 0) {
                        wbuf[wi] = '\0';
                        sc_setvar(fvar, wbuf);
                        sc_exec_block(do_start, done_line);
                        wi = 0;
                    }
                    if (!*fin) break;
                } else {
                    if (wi < 95) wbuf[wi++] = *fin;
                }
                fin++;
            }
            if (wi > 0) { wbuf[wi]='\0'; sc_setvar(fvar, wbuf); sc_exec_block(do_start, done_line); }
            i = done_line + 1;
            continue;
        }

        /* while COND; do */
        if (str_starts_with(line, "while ")) {
            const char *wp = line + 6;
            char wcond[128]; int wci = 0;
            while (wp[wci] && wp[wci] != ';' && wci < 127) wcond[wci] = wp[wci], wci++;
            wcond[wci] = '\0';
            int wdone = sc_find_end(i+1,"done");
            int wdo = i + 1;
            { const char *wl=sc_lines[wdo]; while(*wl==' ')wl++; if(str_compare(wl,"do"))wdo++; }
            if (wdone < 0) wdone = to;
            int max_iter = 1000;
            while (max_iter-- > 0 && sc_eval_cond(wcond))
                sc_exec_block(wdo, wdone);
            i = wdone + 1;
            continue;
        }

        /* skip "then"/"else"/"fi"/"do"/"done" standalone lines */
        if (str_compare(line,"then")||str_compare(line,"else")||str_compare(line,"fi")||
            str_compare(line,"do")  ||str_compare(line,"done")) { i++; continue; }

        /* VAR=value (no spaces, = not first char) */
        {
            int eq = -1, ai2;
            for (ai2 = 0; line[ai2] && line[ai2] != ' '; ai2++) {
                if (line[ai2] == '=') { eq = ai2; break; }
            }
            if (eq > 0 && line[0] != '=') {
                char kbuf[32]; int ki = 0;
                while (ki < eq && ki < 31) kbuf[ki] = line[ki], ki++;
                kbuf[ki] = '\0';
                const char *val = line + eq + 1;
                /* strip quotes */
                if (*val=='"'||*val=='\'') { val++; }
                char vbuf[64]; int vi = 0;
                while (val[vi] && val[vi]!='"' && val[vi]!= '\'' && vi<63) { vbuf[vi]=val[vi]; vi++; }
                vbuf[vi] = '\0';
                sc_setvar(kbuf, vbuf);
                i++; continue;
            }
        }

        /* normal command: copy to input_buffer and execute */
        sc_exec_line(line);
        i++;
    }
    return i;
}

static int sc_exec_line(const char *line) {
    int k = 0;
    while (line[k] && k < 255) { input_buffer[k] = line[k]; k++; }
    input_buffer[k] = '\0';
    input_len = k;
    shell_execute();
    input_buffer[0] = '\0';
    input_len = 0;
    return 0;
}

/* Public: run a .sh script file */
static void shell_run_script(const char *fname) {
    uint32_t sz;
    const uint8_t *data = fs_read_bin(fname, &sz);
    if (!data || sz == 0) {
        set_color(GFX_LRED, GFX_BLACK);
        print("sh: file tidak ditemukan: "); print(fname); print("\n");
        set_color(GFX_WHITE, GFX_BLACK);
        return;
    }
    /* Split into lines */
    sc_nlines = 0;
    int di = 0, li = 0;
    while (di < (int)sz && sc_nlines < SC_MAX_LINES) {
        char c = (char)data[di++];
        if (c == '\r') continue;
        if (c == '\n') {
            sc_lines[sc_nlines][li] = '\0';
            if (li > 0) sc_nlines++;
            li = 0;
        } else if (li < SC_LINE_LEN - 1) {
            sc_lines[sc_nlines][li++] = c;
        }
    }
    if (li > 0) { sc_lines[sc_nlines][li] = '\0'; sc_nlines++; }
    /* Execute */
    sc_exec_block(0, sc_nlines);
}

/* =======================================================================
 * Unified external-program runner helpers
 * ======================================================================= */
#define MAX_PIPE_STAGES 6

/* Parsed single command segment: prog + args + I/O redirect info */
typedef struct {
    char       prog[64];
    char       arg_buf[128];
    const char *argv[9];
    int        argc;
    char       rout[32];  /* stdout redirect file; "" = none */
    char       rin[32];   /* stdin redirect file;  "" = none */
    int        append;    /* 1 = >> (append), 0 = > (truncate) */
    int        bg;        /* 1 = background & */
} ShCmd;

/* Parse one command token (no '|', no '&&'/'||') into ShCmd.
 * Strips leading spaces and optional "exec " prefix. */
static void shcmd_parse(const char *src, ShCmd *c) {
    int i, fi;
    for (i = 0; i < 64;  i++) c->prog[i]    = 0;
    for (i = 0; i < 128; i++) c->arg_buf[i] = 0;
    for (i = 0; i < 32;  i++) c->rout[i]    = 0;
    for (i = 0; i < 32;  i++) c->rin[i]     = 0;
    for (i = 0; i < 9;   i++) c->argv[i]    = 0;
    c->argc = c->append = c->bg = 0;

    while (*src == ' ') src++;
    if (src[0]=='e'&&src[1]=='x'&&src[2]=='e'&&src[3]=='c'&&src[4]==' ') src += 5;
    while (*src == ' ') src++;

    /* Copy prog+args (before '>' or '<') into arg_buf */
    int si = 0, ci = 0;
    while (src[si] && src[si] != '>' && src[si] != '<' && ci < 127)
        c->arg_buf[ci++] = src[si++];
    while (ci > 0 && c->arg_buf[ci-1] == ' ') ci--;
    if (ci > 0 && c->arg_buf[ci-1] == '&') {
        c->bg = 1; ci--;
        while (ci > 0 && c->arg_buf[ci-1] == ' ') ci--;
    }
    c->arg_buf[ci] = '\0';

    /* Tokenize arg_buf → argv */
    char *p = c->arg_buf;
    while (*p == ' ') p++;
    while (*p && c->argc < 8) {
        c->argv[c->argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) { *p = '\0'; p++; while (*p == ' ') p++; }
    }
    c->argv[c->argc] = 0;
    if (c->argc > 0) {
        int ni = 0; const char *s0 = c->argv[0];
        while (s0[ni] && ni < 63) c->prog[ni] = s0[ni], ni++;
        c->prog[ni] = '\0';
    }

    /* Parse redirect operators from src[si..] */
    while (src[si]) {
        if (src[si] == '>') {
            si++;
            if (src[si] == '>') { c->append = 1; si++; }
            while (src[si] == ' ') si++;
            fi = 0;
            while (src[si] && src[si] != ' ' && src[si] != '>' &&
                   src[si] != '<' && fi < 31)
                c->rout[fi++] = src[si++];
            c->rout[fi] = '\0';
        } else if (src[si] == '<') {
            si++;
            while (src[si] == ' ') si++;
            fi = 0;
            while (src[si] && src[si] != ' ' && src[si] != '>' &&
                   src[si] != '<' && fi < 31)
                c->rin[fi++] = src[si++];
            c->rin[fi] = '\0';
        } else if (src[si] == '&') {
            c->bg = 1; si++;
        } else {
            si++;
        }
    }
}

/* Load and start one external program.
 * pipe_in_fd / pipe_out_fd: kernel pipe id or -1 (not used).
 * Returns tid on success, -1 on error (message already printed).
 * Returns -2 if program not found on filesystem (caller handles message). */
static int shcmd_run_nowait(ShCmd *c, int pipe_in_fd, int pipe_out_fd) {
    uint32_t sz;
    const uint8_t *data = fs_read_bin(c->prog, &sz);
    if (!data) return -2;
    uint64_t *dir = vmm_create_page_dir();
    uint64_t entry = elf_load(data, sz, dir);
    if (!entry) {
        set_color(GFX_LRED, GFX_BLACK);
        print(c->prog); print(": gagal load ELF\n");
        set_color(GFX_WHITE, GFX_BLACK);
        return -1;
    }
    uint64_t sp = pmm_alloc_frame();
    vmm_map_page(dir, 0x600000, sp, 7);
    __asm__ volatile("cli" ::: "memory");
    int tid = task_create_user(entry, dir, 0x600000 + PAGE_SIZE,
                               c->prog, c->argc, c->argv);
    if (pipe_out_fd >= 0)  vfs_redirect_out_pipe(tid, pipe_out_fd);
    else if (c->rout[0]) {
        if (c->append) vfs_redirect_out_append(tid, c->rout);
        else           vfs_redirect_out(tid, c->rout);
    }
    if (pipe_in_fd >= 0)   vfs_redirect_in_pipe(tid, pipe_in_fd);
    else if (c->rin[0])    vfs_redirect_in(tid, c->rin);
    __asm__ volatile("sti" ::: "memory");
    return tid;
}

static void shell_execute() {
    last_exit_code = 0;  /* built-in commands default to success */
    print("\n");

    /* Fondasi AC: VAR=value assignment (before $VAR expansion) */
    {
        int eq = -1, ai;
        /* Only assign if first non-space token has '=' and no spaces before '=' */
        for (ai = 0; input_buffer[ai] && input_buffer[ai] != ' '; ai++) {
            if (input_buffer[ai] == '=') { eq = ai; break; }
        }
        if (eq > 0 && input_buffer[0] != '=') {
            char kbuf[32]; int ki = 0;
            while (ki < eq && ki < 31) kbuf[ki] = input_buffer[ki], ki++;
            kbuf[ki] = '\0';
            const char *val = input_buffer + eq + 1;
            if (*val == '"' || *val == '\'') val++;
            char vbuf[64]; int vi = 0;
            while (val[vi] && val[vi] != '"' && val[vi] != '\'' && vi < 63) vbuf[vi++] = val[vi];
            vbuf[vi] = '\0';
            sc_setvar(kbuf, vbuf);
            return;  /* assignment done, no further processing */
        }
    }

    /* F1: ekspansi $VAR */
    shell_expand_vars();

    /* F1: strip trailing '&' → background exec flag.
     * Hati-hati: jangan strip jika '&' adalah bagian dari '&&' operator. */
    int bg_exec = 0;
    {
        int t = 0; while (input_buffer[t]) t++; t--;
        while (t >= 0 && input_buffer[t] == ' ') t--;
        if (t >= 0 && input_buffer[t] == '&') {
            /* Pastikan bukan bagian dari '&&' */
            if (t == 0 || input_buffer[t-1] != '&') {
                bg_exec = 1;
                input_buffer[t] = '\0';
                while (t > 0 && input_buffer[t-1] == ' ') { t--; input_buffer[t] = '\0'; }
            }
        }
    }
    (void)bg_exec; /* dipakai di exec command */

    /* ---- Conditional operators: cmd1 && cmd2 / cmd1 || cmd2 ----
     * Bind looser dari '|', jadi di-split duluan sebelum pipeline check. */
    {
        int ci2 = 0; char cond_op = 0; int cond_pos = -1;
        while (input_buffer[ci2] && input_buffer[ci2+1]) {
            if (input_buffer[ci2] == '&' && input_buffer[ci2+1] == '&') {
                cond_op = 'A'; cond_pos = ci2; break;
            }
            if (input_buffer[ci2] == '|' && input_buffer[ci2+1] == '|') {
                cond_op = 'O'; cond_pos = ci2; break;
            }
            ci2++;
        }
        if (cond_pos >= 0) {
            /* Ekstrak perintah kiri */
            char lbuf[256]; int li2 = 0;
            while (li2 < cond_pos && li2 < 255) lbuf[li2] = input_buffer[li2], li2++;
            while (li2 > 0 && lbuf[li2-1] == ' ') li2--;
            lbuf[li2] = '\0';
            /* Ekstrak perintah kanan */
            const char *rp = input_buffer + cond_pos + 2;
            while (*rp == ' ') rp++;
            char rbuf[256]; int ri2 = 0;
            while (*rp && ri2 < 255) rbuf[ri2++] = *rp++;
            rbuf[ri2] = '\0';
            /* Simpan input_buffer, eksekusi kiri, cek exit code, eksekusi kanan */
            char sv[256]; int sv_len = input_len; int bi2;
            for (bi2 = 0; bi2 <= input_len; bi2++) sv[bi2] = input_buffer[bi2];
            /* Jalankan kiri */
            for (bi2 = 0; lbuf[bi2]; bi2++) input_buffer[bi2] = lbuf[bi2];
            input_buffer[bi2] = '\0'; input_len = bi2;
            shell_execute();
            int lec = last_exit_code;
            /* Jalankan kanan jika kondisi terpenuhi */
            int run_right = (cond_op == 'A') ? (lec == 0) : (lec != 0);
            if (run_right && rbuf[0]) {
                for (bi2 = 0; rbuf[bi2]; bi2++) input_buffer[bi2] = rbuf[bi2];
                input_buffer[bi2] = '\0'; input_len = bi2;
                shell_execute();
            }
            /* Pulihkan */
            for (bi2 = 0; bi2 <= sv_len; bi2++) input_buffer[bi2] = sv[bi2];
            input_len = sv_len;
            return;
        }
    }

    /* ---- Pipeline operator '|': prog1 | prog2 ---- */
    /* ---- Multi-stage pipeline: prog1 | prog2 | ... | progN (maks 6) ---- */
    {
        /* Cek apakah ada '|' (tapi bukan bagian dari '||' yang sudah di-handle) */
        int has_pipe = 0;
        {
            int ic;
            for (ic = 0; input_buffer[ic]; ic++) {
                if (input_buffer[ic] == '|') { has_pipe = 1; break; }
            }
        }
        if (has_pipe) {
            char stage_buf[MAX_PIPE_STAGES][128];
            int  n_stages = 0;
            /* Split input_buffer pada '|' menjadi stage_buf[] */
            {
                int si = 0, bi = 0;
                while (input_buffer[si] && n_stages < MAX_PIPE_STAGES) {
                    if (input_buffer[si] == '|') {
                        while (bi > 0 && stage_buf[n_stages][bi-1] == ' ') bi--;
                        stage_buf[n_stages][bi] = '\0';
                        if (bi > 0) n_stages++;
                        bi = 0;
                    } else if (bi < 127) {
                        stage_buf[n_stages][bi++] = input_buffer[si];
                    }
                    si++;
                }
                while (bi > 0 && stage_buf[n_stages][bi-1] == ' ') bi--;
                stage_buf[n_stages][bi] = '\0';
                if (bi > 0) n_stages++;
            }
            if (n_stages < 2) {
                set_color(GFX_LRED, GFX_BLACK);
                print("pipe: butuh setidaknya dua perintah\n");
                set_color(GFX_WHITE, GFX_BLACK);
                return;
            }
            /* Alokasikan n_stages-1 pipe */
            int pipes[MAX_PIPE_STAGES - 1];
            int np, ok = 1;
            for (np = 0; np < n_stages - 1; np++) {
                pipes[np] = pipe_alloc();
                if (pipes[np] < 0) {
                    int q; for (q = 0; q < np; q++) pipe_free(pipes[q]);
                    set_color(GFX_LRED, GFX_BLACK); print("pipe: gagal alokasi\n");
                    set_color(GFX_WHITE, GFX_BLACK); ok = 0; break;
                }
            }
            if (!ok) return;
            /* Start setiap stage */
            int tids[MAX_PIPE_STAGES];
            for (np = 0; np < n_stages; np++) {
                ShCmd sc;
                shcmd_parse(stage_buf[np], &sc);
                int in_fd  = (np > 0)            ? pipes[np-1] : -1;
                int out_fd = (np < n_stages - 1) ? pipes[np]   : -1;
                tids[np] = shcmd_run_nowait(&sc, in_fd, out_fd);
                if (tids[np] == -2) {
                    set_color(GFX_LRED, GFX_BLACK);
                    print(sc.prog); print(": tidak ditemukan\n");
                    set_color(GFX_WHITE, GFX_BLACK);
                }
            }
            /* Tunggu semua stage */
            keyboard_set_fg_pid(tids[0] >= 0 ? tids[0] : -1);
            for (np = 0; np < n_stages; np++) {
                if (tids[np] >= 0) task_wait(tids[np]);
            }
            keyboard_set_fg_pid(-1);
            last_exit_code = (tids[n_stages-1] >= 0)
                             ? task_get_exit_code(tids[n_stages-1]) : 1;
            for (np = 0; np < n_stages - 1; np++) pipe_free(pipes[np]);
            return;
        }
    }

    if(str_compare(input_buffer, "help")){
        set_color(GFX_YELLOW, GFX_BLACK);
        print("Perintah yang tersedia:\n");
        set_color(GFX_WHITE, GFX_BLACK);
        print("help                 - tampilkan daftar perintah\n");
        print("clear                - bersihkan layar\n");
        print("gui                  - aktifkan mode GUI (desktop + wallpaper)\n");
        print("about                - informasi tentang Oria OS\n");
        print("memtest              - test alokasi memory\n");
        print("uptime               - tampilkan waktu berjalan OS\n");
        print("date                 - tampilkan tanggal dan jam (RTC)\n");
        print("echo <text>          - tampilkan text\n");
        print("time                 - tampilkan ticks sejak boot\n");
        print("reboot               - reboot sistem\n");
        print("shutdown             - matikan sistem (ACPI poweroff)\n");
        print("poweroff             - sama dengan shutdown\n");
        print("ls                   - tampilkan daftar file (di direktori saat ini)\n");
        print("cd <dir>             - pindah direktori (cd .. / cd / untuk root)\n");
        print("pwd                  - tampilkan direktori saat ini\n");
        print("read <nama>          - baca file\n");
        print("write <nama> <isi>   - simpan file\n");
        print("del <nama>           - hapus file\n");
        print("rename <lama> <baru> - ganti nama file (MFS4)\n");
        print("mkdir <nama>         - buat direktori\n");
        print("chmod <nama> <hex>   - ubah permission file\n");
        print("sync                 - flush dirty file ke disk\n");
        print("export KEY=VAL       - set environment variable\n");
        print("env                  - tampilkan semua env var\n");
        print("ifconfig             - tampilkan info jaringan (MAC, IP, GW)\n");
        print("ping <ip>            - kirim 4 ICMP echo request ke IP\n");
        print("udp_send <ip> <port> <pesan> - kirim UDP datagram\n");
        print("tcp_get <ip> <port> [path]   - HTTP GET via TCP (demo koneksi internet)\n");
        print("nslookup <hostname>          - resolve DNS A record via 8.8.8.8\n");
        print("curl <url>                   - HTTP GET, tampilkan response (http://host/path)\n");
        print("ntpdate              - sinkronisasi waktu via NTP (pool.ntp.org)\n");
        print("httpd [start [port]] - jalankan HTTP server (default port 8080)\n");
        print("httpd stop           - stop HTTP server\n");
        print("ws <url>             - WebSocket client (ws://host/path)\n");
        print("vdisk                - tampilkan info VirtIO block device\n");
        print("vdisk read <sector>  - baca 1 sektor dari VirtIO disk (hex dump)\n");
        print("cpuinfo              - tampilkan info SMP (BSP/AP online)\n");
        print("taskstat             - tampilkan distribusi task per CPU\n");
        print("meminfo              - tampilkan statistik memori fisik & heap\n");
        print("threadtest           - demo threading: spawn 3 thread secara paralel\n");
        print("futextest            - demo futex+TLS: 4 thread increment counter 4000x\n");
        print("polltest             - demo non-blocking fd + poll(): pipe poll dengan timeout\n");
        print("condtest             - demo condition variable: producer-consumer\n");
        print("open <file>          - buka file, cetak fd\n");
        print("fread <fd>           - baca isi file lewat fd\n");
        print("fwrite <fd> <teks>   - tulis teks ke file lewat fd\n");
        print("fclose <fd>          - tutup fd\n");
        print("mq_send <pid> <msg>  - kirim pesan ke task pid\n");
        print("mq_recv              - terima pesan dari mailbox shell\n");
        print("paging               - tampilkan status paging\n");
        print("exec <nama> [&]           - jalankan program ELF (& = background)\n");
        print("exec <nama> > <file>      - jalankan program, stdout ke file (truncate)\n");
        print("exec <nama> >> <file>     - jalankan program, stdout ke file (append)\n");
        print("exec <nama> < <file>      - jalankan program, stdin dari file\n");
        print("<prog> [args] [>/>>/<] [&] - jalankan program langsung (tanpa 'exec')\n");
        print("prog1 | prog2 | prog3     - pipeline (maksimal 6 stage)\n");
        print("cmd1 && cmd2              - jalankan cmd2 hanya jika cmd1 berhasil\n");
        print("cmd1 || cmd2              - jalankan cmd2 hanya jika cmd1 gagal\n");
        print("ps                   - tampilkan daftar proses\n");
        print("kill <id>            - matikan proses berdasarkan ID\n");
        print("setprio <id> <1-3>   - ubah priority proses\n");
    }
    else if(str_compare(input_buffer, "clear")){
        clear_screen();
    }
    else if(str_compare(input_buffer, "gui")){
        /* Aktifkan GUI mode: wallpaper + window manager + mouse */
        set_color(GFX_LGRAY, GFX_BLACK);
        print("Memuat GUI...\n");
        wm_init();
        mouse_init();
        g_gui_mode = 1;
        /* Layar sudah digambar oleh wm_init (wp_blit) */
        /* Di sini kita TIDAK kembali ke prompt — loop kernel ambil alih */
    }
    else if(str_compare(input_buffer, "ps")){
        /* Fondasi AH — list proses dari /proc/ps */
        int tid = task_get_current();
        int fd = vfs_open(tid, "/proc/ps", VFS_O_RDONLY);
        if (fd < 0) { print("ps: /proc/ps tidak tersedia\n"); }
        else {
            char pbuf[128]; int n;
            while ((n = vfs_read(tid, fd, pbuf, 127)) > 0) {
                pbuf[n] = '\0'; print(pbuf);
            }
            vfs_close(tid, fd);
        }
    }
    else if(str_compare(input_buffer, "uptime")){
        /* Fondasi AH — tampilkan uptime dari /proc/uptime */
        int tid = task_get_current();
        int fd = vfs_open(tid, "/proc/uptime", VFS_O_RDONLY);
        if (fd < 0) { print("uptime: tidak tersedia\n"); }
        else {
            char pbuf[64]; int n;
            while ((n = vfs_read(tid, fd, pbuf, 63)) > 0) {
                pbuf[n] = '\0'; print(pbuf);
            }
            vfs_close(tid, fd);
        }
    }
    else if(str_compare(input_buffer, "history")){
        if (hist_count == 0) {
            print("(history kosong)\n");
        } else {
            int i;
            for (i = hist_count - 1; i >= 0; i--) {
                int idx = (hist_head - 1 - i + HISTORY_SIZE * 8) % HISTORY_SIZE;
                char nb[8]; itoa((uint32_t)(hist_count - i), nb);
                set_color(GFX_CYAN, GFX_BLACK);
                print(nb); print("\t");
                set_color(GFX_WHITE, GFX_BLACK);
                print(history[idx]); print("\n");
            }
        }
    }
    else if(str_starts_with(input_buffer, "sh ") || str_compare(input_buffer, "sh")){
        /* Fondasi AC — jalankan script .sh */
        if (str_compare(input_buffer, "sh")) {
            print("Penggunaan: sh <file.sh>\n");
        } else {
            const char *fn = input_buffer + 3;
            while (*fn == ' ') fn++;
            char sc_path[64]; int pi = 0;
            /* make_path() equivalent */
            if (current_dir[0]) {
                while (current_dir[pi] && pi < 61) sc_path[pi] = current_dir[pi], pi++;
                sc_path[pi++] = '/';
            }
            int fi2 = 0;
            while (fn[fi2] && pi < 63) sc_path[pi++] = fn[fi2++];
            sc_path[pi] = '\0';
            shell_run_script(sc_path);
        }
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
    else if(str_compare(input_buffer, "date")){
        /* Baca RTC dan tampilkan tanggal + jam */
        RtcTime rt;
        rtc_read(&rt);
        char buf[6];
        /* Format: YYYY-MM-DD HH:MM:SS */
        /* tahun */
        char ybuf[8];
        itoa((uint32_t)rt.year, ybuf);
        print(ybuf); print("-");
        /* bulan */
        if (rt.month < 10) print("0");
        itoa(rt.month,  buf); print(buf); print("-");
        /* hari */
        if (rt.day < 10) print("0");
        itoa(rt.day,    buf); print(buf); print(" ");
        /* jam */
        if (rt.hour < 10) print("0");
        itoa(rt.hour,   buf); print(buf); print(":");
        /* menit */
        if (rt.minute < 10) print("0");
        itoa(rt.minute, buf); print(buf); print(":");
        /* detik */
        if (rt.second < 10) print("0");
        itoa(rt.second, buf); print(buf); print("\n");
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
        acpi_reboot();
    }
    else if(str_compare(input_buffer, "shutdown") || str_compare(input_buffer, "poweroff")){
        print("Shutting down...\n");
        acpi_shutdown();
    }
    else if(str_compare(input_buffer, "ls") || str_compare(input_buffer, "ls -l")){
        int long_fmt = str_compare(input_buffer, "ls -l");
        set_color(GFX_YELLOW, GFX_BLACK);
        if (current_dir[0]) {
            print("Isi direktori /"); print(current_dir); print(":\n");
        } else {
            print("Daftar file:\n");
        }
        set_color(GFX_WHITE, GFX_BLACK);
        if (!long_fmt) {
            if (current_dir[0]) fs_list_dir(current_dir, print);
            else                fs_list(print);
        } else {
            /* ls -l: tampilkan perms, mtime, ukuran, nama */
            static char lsbuf[4096];
            int lsn = fs_list_buf(lsbuf, 4096);
            if (lsn <= 0) { print("(kosong)\n"); }
            else {
                set_color(GFX_LCYAN, GFX_BLACK);
                print("PERMS    MTIME        SIZE  NAMA\n");
                print("-------- ------------ ----- --------------------------------\n");
                set_color(GFX_WHITE, GFX_BLACK);
                int li = 0;
                while (lsbuf[li]) {
                    char fname[64]; int fi = 0;
                    while (lsbuf[li] && lsbuf[li] != '\n' && fi < 63)
                        fname[fi++] = lsbuf[li++];
                    fname[fi] = '\0';
                    if (lsbuf[li] == '\n') li++;
                    if (fi == 0) continue;
                    /* filter direktori sesuai current_dir */
                    if (current_dir[0]) {
                        if (!str_starts_with(fname, current_dir)) continue;
                        /* hanya entri langsung di direktori ini (tidak ada '/' lagi setelah prefix) */
                        const char *sub = fname + str_len(current_dir);
                        if (*sub == '/') sub++;
                        int has_slash = 0; const char *sp = sub;
                        while (*sp) { if (*sp == '/') { has_slash=1; break; } sp++; }
                        if (has_slash) continue;
                    }
                    MFS4Stat st;
                    if (mfs4_stat(fname, &st) != 0) continue;
                    /* Perms: rwx format */
                    char pstr[10];
                    pstr[0] = (st.type == MFS4_TYPE_DIR) ? 'd' : '-';
                    pstr[1] = (st.perms & 0x0004) ? 'r' : '-';
                    pstr[2] = (st.perms & 0x0002) ? 'w' : '-';
                    pstr[3] = (st.perms & 0x0001) ? 'x' : '-';
                    pstr[4] = '-'; pstr[5] = '-'; pstr[6] = '-';
                    pstr[7] = '-'; pstr[8] = '\0';
                    set_color((st.type == MFS4_TYPE_DIR) ? GFX_LCYAN : GFX_WHITE, GFX_BLACK);
                    print(pstr); print(" ");
                    char nb[12]; itoa(st.mtime, nb); print(nb);
                    /* pad mtime to 12 chars */
                    int mlen = 0; while (nb[mlen]) mlen++;
                    while (mlen++ < 12) print(" ");
                    print(" ");
                    /* size right-aligned to 5 */
                    itoa(st.size, nb);
                    int slen = 0; while (nb[slen]) slen++;
                    while (slen++ < 5) print(" ");
                    print(nb); print("  ");
                    print(fname); print("\n");
                }
                set_color(GFX_WHITE, GFX_BLACK);
            }
        }
    }
    else if(str_starts_with(input_buffer, "ls ")) {
        /* ls <dir> — tampilkan isi direktori tertentu */
        const char *lsdir = input_buffer + 3;
        while (*lsdir == ' ') lsdir++;
        set_color(GFX_YELLOW, GFX_BLACK);
        print("Isi direktori /"); print(lsdir); print(":\n");
        set_color(GFX_WHITE, GFX_BLACK);
        fs_list_dir(lsdir, print);
    }
    /* ================================================================
     * Fondasi AZ — df: disk free/usage MFS4
     * ================================================================ */
    else if (str_compare(input_buffer, "df")) {
        int total_files = FS_MAX_FILES;
        /* hitung file yang terpakai */
        static char dfbuf[4096];
        int dfn = fs_list_buf(dfbuf, 4096);
        int used_files = 0;
        if (dfn > 0) {
            int di = 0;
            while (dfbuf[di]) {
                char dfname[64]; int dfi = 0;
                while (dfbuf[di] && dfbuf[di] != '\n' && dfi < 63) dfname[dfi++] = dfbuf[di++];
                dfname[dfi] = '\0';
                if (dfbuf[di] == '\n') di++;
                if (dfi > 0) used_files++;
            }
        }
        int free_files = total_files - used_files;
        uint32_t used_bytes = (uint32_t)used_files * FS_MAX_DATA;
        uint32_t total_bytes = (uint32_t)total_files * FS_MAX_DATA;
        uint32_t free_bytes  = total_bytes - used_bytes;
        char nb[16];
        set_color(GFX_YELLOW, GFX_BLACK);
        print("Filesystem: MFS4 (in-memory)\n");
        set_color(GFX_WHITE, GFX_BLACK);
        print("  Total    : "); itoa(total_files, nb); print(nb); print(" slot  = ");
        itoa(total_bytes / 1024, nb); print(nb); print(" KB\n");
        print("  Terpakai : "); itoa(used_files, nb); print(nb); print(" slot  = ");
        itoa(used_bytes / 1024, nb); print(nb); print(" KB\n");
        set_color(GFX_LGREEN, GFX_BLACK);
        print("  Bebas    : "); itoa(free_files, nb); print(nb); print(" slot  = ");
        itoa(free_bytes / 1024, nb); print(nb); print(" KB\n");
        set_color(GFX_WHITE, GFX_BLACK);
    }
    /* ================================================================
     * Fondasi AZ — du: directory/file size
     * ================================================================ */
    else if (str_compare(input_buffer, "du")) {
        print("Penggunaan: du <file|dir>\n");
    }
    else if (str_starts_with(input_buffer, "du ")) {
        const char *dupath = input_buffer + 3;
        while (*dupath == ' ') dupath++;
        /* Hitung total ukuran semua file yang namanya diawali path ini */
        static char dubuf[4096];
        int dun = fs_list_buf(dubuf, 4096);
        uint32_t total_sz = 0;
        int total_files2 = 0;
        if (dun > 0) {
            int di = 0;
            while (dubuf[di]) {
                char duname[64]; int dfi = 0;
                while (dubuf[di] && dubuf[di] != '\n' && dfi < 63) duname[dfi++] = dubuf[di++];
                duname[dfi] = '\0';
                if (dubuf[di] == '\n') di++;
                if (dfi == 0) continue;
                if (!str_starts_with(duname, dupath)) continue;
                MFS4Stat dust;
                if (mfs4_stat(duname, &dust) == 0) {
                    total_sz += dust.size;
                    total_files2++;
                }
            }
        }
        char nb[16];
        set_color(GFX_LCYAN, GFX_BLACK);
        print(dupath); print(": ");
        set_color(GFX_WHITE, GFX_BLACK);
        itoa(total_files2, nb); print(nb); print(" file, ");
        itoa(total_sz, nb); print(nb); print(" bytes");
        if (total_sz >= 1024) { print(" ("); itoa(total_sz/1024, nb); print(nb); print(" KB)"); }
        print("\n");
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
        /* Fondasi AZ — read VARNAME: jika argument hanya identifier (tanpa '/' atau '.'),
         * baca satu baris dari keyboard dan simpan ke variabel. */
        const char *rarg = input_buffer + 5;
        while (*rarg == ' ') rarg++;
        int is_var = 1;
        { const char *rp = rarg;
          while (*rp && *rp != ' ') {
              if (*rp == '/' || *rp == '.' || *rp == '\\') { is_var = 0; break; }
              if (!( (*rp>='a'&&*rp<='z') || (*rp>='A'&&*rp<='Z') || (*rp>='0'&&*rp<='9') || *rp=='_' )) { is_var = 0; break; }
              rp++;
          }
        }
        if (is_var && rarg[0] != '\0') {
            /* read VARNAME: baca satu baris dari keyboard */
            char vname2[24]; int vi2 = 0;
            while (rarg[vi2] && rarg[vi2] != ' ' && vi2 < 23) { vname2[vi2] = rarg[vi2]; vi2++; }
            vname2[vi2] = '\0';
            char linebuf[128]; int li2 = 0;
            char rc2;
            while (li2 < 127) {
                rc2 = keyboard_getchar_block();
                if (rc2 == '\n') break;
                if (rc2 == '\b') { if (li2 > 0) { li2--; backspace_char(); } continue; }
                linebuf[li2++] = rc2;
                print_char(rc2);
            }
            linebuf[li2] = '\0'; print("\n");
            sc_setvar(vname2, linebuf);
        } else {
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
    else if (str_starts_with(input_buffer, "rename ")) {
        /* rename <lama> <baru>: pisahkan dua argumen nama file */
        const char *args = input_buffer + 7;
        int sp = str_find_space(args);
        if (sp < 0) {
            print("gunakan: rename <nama_lama> <nama_baru>\n");
        } else {
            char old_buf[64], new_buf[64], op[64], np[64];
            int i;
            for (i = 0; i < sp && i < 63; i++) old_buf[i] = args[i];
            old_buf[sp < 63 ? sp : 63] = '\0';
            const char *rest = args + sp + 1;
            for (i = 0; rest[i] && i < 63; i++) new_buf[i] = rest[i];
            new_buf[i] = '\0';
            const char *oldp = make_path(old_buf, op, 64);
            const char *newp = make_path(new_buf, np, 64);
            int rc = mfs4_rename(oldp, newp);
            if (rc == 0) {
                set_color(GFX_LGREEN, GFX_BLACK);
                print("Renamed: "); print(oldp); print(" -> "); print(newp); print("\n");
                set_color(GFX_WHITE, GFX_BLACK);
            } else {
                set_color(GFX_LRED, GFX_BLACK);
                print("rename gagal (file tidak ada atau tujuan sudah ada)\n");
                set_color(GFX_WHITE, GFX_BLACK);
            }
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
        /* Gunakan helper shcmd_parse — mendukung >, >>, <, & dan seterusnya */
        ShCmd ec;
        shcmd_parse(input_buffer + 5, &ec);
        if (!ec.prog[0]) {
            set_color(GFX_LRED, GFX_BLACK); print("exec: nama program kosong\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else {
            int bg = ec.bg || bg_exec;
            ec.bg = 0;  /* shcmd_run_nowait tidak menunggu; kita handle di sini */
            int tid = shcmd_run_nowait(&ec, -1, -1);
            if (tid == -2) {
                print("exec: file tidak ditemukan: "); print(ec.prog); print("\n");
            } else if (tid >= 0) {
                if (bg) {
                    char tbuf[8]; itoa((uint32_t)tid, tbuf);
                    print("["); print(tbuf); print("] "); print(ec.prog); print(" &\n");
                } else {
                    keyboard_set_fg_pid(tid);
                    task_wait(tid);
                    last_exit_code = task_get_exit_code(tid);
                    keyboard_set_fg_pid(-1);
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
            if (task_is_thread(i)) {
                set_color(GFX_LCYAN, GFX_BLACK);
                print("[T:"); itoa((uint32_t)task_get_parent(i), buf); print(buf); print("] ");
                set_color(GFX_WHITE, GFX_BLACK);
            }
            print(task_get_name(i));
            print("\n");
        }
    }
    else if(str_starts_with(input_buffer, "kill ")) {
        /* Format: kill [-N] <id>
         * -N = nomor sinyal (2=SIGINT, 9=SIGKILL, 15=SIGTERM)
         * Default: SIGKILL */
        const char *p = input_buffer + 5;
        int sig = 9; /* SIGKILL default */
        if (*p == '-') {
            p++;
            int sn = 0;
            while (*p >= '0' && *p <= '9') { sn = sn * 10 + (*p - '0'); p++; }
            if (sn > 0) sig = sn;
            while (*p == ' ') p++;
        }
        int id = 0;
        while (*p >= '0' && *p <= '9') { id = id * 10 + (*p - '0'); p++; }
        if (id == 0) {
            set_color(GFX_LRED, GFX_BLACK);
            print("kill: tidak dapat mematikan shell (id 0)\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else if (sig == 9 || sig == 0) {
            /* SIGKILL: langsung terminate */
            if (task_kill(id)) {
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
        } else {
            /* Kirim sinyal (SIGINT=2, SIGTERM=15, dll) */
            task_send_signal(id, sig);
            set_color(GFX_LGREEN, GFX_BLACK);
            print("kill: sinyal "); char sbuf[8]; itoa(sig, sbuf); print(sbuf);
            print(" dikirim ke proses "); char idbuf[8]; itoa(id, idbuf); print(idbuf);
            print("\n");
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
    /* ================================================================
     * Fondasi AW — renice <nice> <pid>  : ubah nice value task
     * ================================================================ */
    else if (str_starts_with(input_buffer, "renice ")) {
        const char *p = input_buffer + 7;
        /* nice value bisa negatif */
        int sign = 1;
        if (*p == '-') { sign = -1; p++; }
        else if (*p == '+') p++;
        int nv = 0;
        while (*p >= '0' && *p <= '9') { nv = nv * 10 + (*p - '0'); p++; }
        nv *= sign;
        while (*p == ' ') p++;
        int pid = 0;
        while (*p >= '0' && *p <= '9') { pid = pid * 10 + (*p - '0'); p++; }
        if (!task_set_nice(pid, nv)) {
            set_color(GFX_LRED, GFX_BLACK);
            print("renice: proses tidak ditemukan\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else {
            set_color(GFX_LGREEN, GFX_BLACK);
            print("renice: proses "); char buf[8]; itoa(pid, buf); print(buf);
            print(" nice="); itoa((uint32_t)(nv < 0 ? (uint32_t)(-nv) : (uint32_t)nv), buf);
            if (nv < 0) { print("-"); } print(buf); print("\n");
            set_color(GFX_WHITE, GFX_BLACK);
        }
    }
    /* ================================================================
     * Fondasi AZ — nice <nice> <cmd>: jalankan perintah dengan nice value
     * ================================================================ */
    else if (str_starts_with(input_buffer, "nice ")) {
        const char *p = input_buffer + 5;
        int sign = 1;
        if (*p == '-') { sign = -1; p++; }
        else if (*p == '+') p++;
        int nv = 0;
        while (*p >= '0' && *p <= '9') { nv = nv * 10 + (*p - '0'); p++; }
        nv *= sign;
        while (*p == ' ') p++;
        if (!*p) {
            print("nice: gunakan nice [-N] <perintah>\n");
        } else {
            /* Jalankan perintah dan set nice setelah spawn */
            ShCmd nc;
            shcmd_parse(p, &nc);
            if (!nc.prog[0]) {
                set_color(GFX_LRED, GFX_BLACK); print("nice: nama program kosong\n");
                set_color(GFX_WHITE, GFX_BLACK);
            } else {
                int ntid = shcmd_run_nowait(&nc, -1, -1);
                if (ntid == -2) {
                    print("nice: program tidak ditemukan: "); print(nc.prog); print("\n");
                } else if (ntid >= 0) {
                    task_set_nice(ntid, nv);
                    char nbuf[8]; itoa((uint32_t)ntid, nbuf);
                    set_color(GFX_LGREEN, GFX_BLACK);
                    print("nice: "); print(nc.prog); print(" [tid="); print(nbuf);
                    print("] nice=");
                    itoa((uint32_t)(nv < 0 ? (uint32_t)(-nv) : (uint32_t)nv), nbuf);
                    if (nv < 0) print("-"); print(nbuf); print("\n");
                    set_color(GFX_WHITE, GFX_BLACK);
                    keyboard_set_fg_pid(ntid);
                    task_wait(ntid);
                    last_exit_code = task_get_exit_code(ntid);
                    keyboard_set_fg_pid(-1);
                }
            }
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
                    const char *av1p[2] = { prog1, 0 };
                    int tid1 = task_create_user(entry1, dir1, 0x600000 + PAGE_SIZE, prog1, 1, av1p);
                    task_set_pipe(tid1, pipe_fd);
                }

                /* Buat dan jalankan prog2 (reader) */
                uint64_t *dir2 = vmm_create_page_dir();
                uint64_t entry2 = elf_load(d2, sz2, dir2);
                if (entry2) {
                    uint64_t sp2 = pmm_alloc_frame();
                    vmm_map_page(dir2, 0x600000, sp2, 7);
                    const char *av2p[2] = { prog2, 0 };
                    int tid2 = task_create_user(entry2, dir2, 0x600000 + PAGE_SIZE, prog2, 1, av2p);
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
    /* ================================================================
     * Fondasi AU — dhcp: jalankan DHCP untuk mendapat IP otomatis
     * ================================================================ */
    else if (str_compare(input_buffer, "dhcp")) {
        if (!dhcp_request()) last_exit_code = 1;
    }
    /* ================================================================
     * Fondasi AV — vt <n>: pindah ke virtual terminal n (0..5)
     * ================================================================ */
    else if (str_starts_with(input_buffer, "vt ")) {
        extern void vt_switch(int n);
        extern int  g_active_vt;
        int n = input_buffer[3] - '0';
        if (n < 0 || n > 5) {
            set_color(GFX_LRED, GFX_BLACK);
            print("vt: nomor terminal 0..5\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else if (n == g_active_vt) {
            print("vt: sudah di tty"); char buf[4]; itoa((uint32_t)n, buf); print(buf); print("\n");
        } else {
            vt_switch(n);
        }
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
    /* --- Fondasi AP: ping6 --- */
    else if (str_compare(input_buffer, "ping6")) {
        print("gunakan ping6 <ipv6>  contoh: ping6 fe80::2\n");
    }
    else if (str_starts_with(input_buffer, "ping6 ")) {
        /* Parse IPv6 address sederhana: fe80::2 atau xx:xx:xx:xx:xx:xx:xx:xx */
        const char *p = input_buffer + 6;
        uint8_t ip6[16];
        int ok = 0;
        /* Hanya handle bentuk "fe80::X" dan "xx:xx::yy" dengan expand :: */
        {
            /* Simpan bagian sebelum :: dan sesudah :: */
            uint16_t grp_left[8], grp_right[8];
            int nl=0, nr=0;
            const char *pp = p;
            int found_dc = 0; /* :: ditemukan */
            const char *dc_pos = 0;
            /* Cari :: */
            while (*pp) {
                if (pp[0]==':' && pp[1]==':') { dc_pos = pp; found_dc=1; break; }
                pp++;
            }
            /* Parse bagian kiri */
            pp = p;
            while (*pp) {
                if (found_dc && pp == dc_pos) break;
                if (*pp == ':') { pp++; continue; }
                /* Baca satu grup hex */
                uint16_t g = 0;
                while ((*pp>='0'&&*pp<='9')||(*pp>='a'&&*pp<='f')||(*pp>='A'&&*pp<='F')) {
                    uint8_t d;
                    if (*pp>='0'&&*pp<='9') d=(uint8_t)(*pp-'0');
                    else if (*pp>='a'&&*pp<='f') d=(uint8_t)(*pp-'a'+10);
                    else d=(uint8_t)(*pp-'A'+10);
                    g=(uint16_t)((g<<4)|d); pp++;
                }
                if (nl < 8) grp_left[nl++] = g;
                if (*pp == ':' && pp[1] != ':') pp++;
            }
            /* Parse bagian kanan (setelah ::) */
            if (found_dc) {
                pp = dc_pos + 2;
                while (*pp) {
                    if (*pp == ':') { pp++; continue; }
                    uint16_t g = 0;
                    while ((*pp>='0'&&*pp<='9')||(*pp>='a'&&*pp<='f')||(*pp>='A'&&*pp<='F')) {
                        uint8_t d;
                        if (*pp>='0'&&*pp<='9') d=(uint8_t)(*pp-'0');
                        else if (*pp>='a'&&*pp<='f') d=(uint8_t)(*pp-'a'+10);
                        else d=(uint8_t)(*pp-'A'+10);
                        g=(uint16_t)((g<<4)|d); pp++;
                    }
                    if (nr < 8) grp_right[nr++] = g;
                    if (*pp == ':') pp++;
                }
            }
            if (nl + nr <= 8) {
                int zeros = 8 - nl - nr;
                int i;
                for (i=0;i<nl;i++) {
                    ip6[i*2]=(uint8_t)(grp_left[i]>>8);
                    ip6[i*2+1]=(uint8_t)(grp_left[i]&0xFF);
                }
                for (i=0;i<zeros;i++) {
                    ip6[(nl+i)*2]=0; ip6[(nl+i)*2+1]=0;
                }
                for (i=0;i<nr;i++) {
                    ip6[(nl+zeros+i)*2]=(uint8_t)(grp_right[i]>>8);
                    ip6[(nl+zeros+i)*2+1]=(uint8_t)(grp_right[i]&0xFF);
                }
                ok = 1;
            }
        }
        if (!ok) {
            set_color(GFX_LRED, GFX_BLACK);
            print("ping6: format alamat IPv6 tidak valid\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else {
            net_ping6(ip6, 4);
        }
    }
    else if (str_starts_with(input_buffer, "udp_send ")) {
        /* Syntax: udp_send <ip> <port> <pesan> */
        const char *p = input_buffer + 9;
        uint8_t ip[4];
        if (!parse_ip(p, ip)) {
            print("udp_send: IP tidak valid\n");
        } else {
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            uint16_t port = 0;
            while (*p >= '0' && *p <= '9') { port = (uint16_t)(port * 10 + (*p - '0')); p++; }
            while (*p == ' ') p++;
            if (!*p || port == 0) {
                print("udp_send: gunakan: udp_send <ip> <port> <pesan>\n");
            } else {
                int n = 0; while (p[n]) n++;
                if (net_udp_send(ip, port, 54321, p, (uint16_t)n) == 0) {
                    set_color(GFX_LGREEN, GFX_BLACK);
                    print("udp: terkirim.\n");
                    set_color(GFX_WHITE, GFX_BLACK);
                } else {
                    set_color(GFX_LRED, GFX_BLACK);
                    print("udp: gagal (ARP timeout?)\n");
                    set_color(GFX_WHITE, GFX_BLACK);
                }
            }
        }
    }
    else if (str_starts_with(input_buffer, "tcp_get ")) {
        /* Syntax: tcp_get <ip> <port> [path]
         * Kirim HTTP GET ke ip:port, cetak response. */
        const char *p = input_buffer + 8;
        uint8_t ip[4];
        if (!parse_ip(p, ip)) {
            print("tcp_get: IP tidak valid\n");
        } else {
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            uint16_t port = 0;
            while (*p >= '0' && *p <= '9') { port = (uint16_t)(port * 10 + (*p - '0')); p++; }
            while (*p == ' ') p++;
            const char *path = (*p) ? p : "/";
            if (port == 0) {
                print("tcp_get: gunakan: tcp_get <ip> <port> [path]\n");
            } else {
                char ip_str[20]; int ii;
                for (ii = 0; ii < 20; ii++) ip_str[ii] = '\0';
                /* print connecting message */
                print("tcp: connecting to ");
                char tbuf[8];
                itoa((uint32_t)ip[0], tbuf); print(tbuf); print(".");
                itoa((uint32_t)ip[1], tbuf); print(tbuf); print(".");
                itoa((uint32_t)ip[2], tbuf); print(tbuf); print(".");
                itoa((uint32_t)ip[3], tbuf); print(tbuf); print(":");
                itoa(port, tbuf); print(tbuf); print("...\n");
                int id = net_tcp_connect(ip, port);
                if (id < 0) {
                    set_color(GFX_LRED, GFX_BLACK);
                    if (id == -2)
                        print("tcp: connection refused (RST dari server)\n");
                    else
                        print("tcp: timeout — tidak ada respons dalam 5 detik\n");
                    set_color(GFX_WHITE, GFX_BLACK);
                } else {
                    /* Send HTTP GET */
                    char req[256];
                    int ri = 0;
                    const char *p1 = "GET "; const char *p2 = path;
                    const char *p3 = " HTTP/1.0\r\nHost: ";
                    const char *p4 = "\r\nConnection: close\r\n\r\n";
                    while (*p1) req[ri++] = *p1++;
                    while (*p2 && ri < 200) req[ri++] = *p2++;
                    while (*p3) req[ri++] = *p3++;
                    /* Host header: ip as string */
                    int oi;
                    for (oi = 0; oi < 4; oi++) {
                        itoa((uint32_t)ip[oi], tbuf);
                        const char *tp = tbuf; while (*tp) req[ri++] = *tp++;
                        if (oi < 3) req[ri++] = '.';
                    }
                    while (*p4) req[ri++] = *p4++;
                    net_tcp_send(id, req, (uint16_t)ri);
                    set_color(GFX_LGREEN, GFX_BLACK);
                    print("tcp: connected. menunggu response...\n");
                    set_color(GFX_WHITE, GFX_BLACK);
                    /* Receive & print response */
                    char rbuf[256];
                    int got, total = 0;
                    while ((got = net_tcp_recv(id, rbuf, 255)) > 0) {
                        int gi;
                        for (gi = 0; gi < got; gi++) {
                            char c = rbuf[gi];
                            /* print printable + newlines */
                            if ((c >= 0x20 && c < 0x7F) || c == '\n' || c == '\t') {
                                char cc[2]; cc[0] = c; cc[1] = '\0';
                                print(cc);
                            } else if (c == '\r') { /* skip CR */ }
                        }
                        total += got;
                    }
                    net_tcp_close(id);
                    set_color(GFX_LCYAN, GFX_BLACK);
                    print("\ntcp: selesai, total ");
                    itoa((uint32_t)total, tbuf); print(tbuf);
                    print(" bytes.\n");
                    set_color(GFX_WHITE, GFX_BLACK);
                }
            }
        }
    }
    /* F-Y3: curl <url> — HTTP GET */
    else if (str_starts_with(input_buffer, "curl ")) {
        const char *url = input_buffer + 5;
        while (*url == ' ') url++;
        if (!*url) {
            print("curl: gunakan: curl http[s]://hostname/path\n");
        } else if (url[0]=='h'&&url[1]=='t'&&url[2]=='t'&&url[3]=='p'&&url[4]=='s') {
            https_get(url);
        } else {
            http_get(url);
        }
    }
    /* --- Fondasi AQ: certcheck <hostname> --- */
    else if (str_compare(input_buffer, "certcheck")) {
        print("gunakan certcheck <hostname>  contoh: certcheck example.com\n");
    }
    else if (str_starts_with(input_buffer, "certcheck ")) {
        const char *host = input_buffer + 10;
        while (*host == ' ') host++;
        if (!*host) {
            print("certcheck: gunakan: certcheck <hostname>\n");
        } else {
            uint8_t ip[4];
            print("certcheck: resolving "); print(host); print("...\n");
            if (!dns_resolve(host, ip)) {
                set_color(GFX_LRED, GFX_BLACK);
                print("certcheck: DNS gagal\n");
                set_color(GFX_WHITE, GFX_BLACK);
            } else {
                int tid = net_tcp_connect(ip, 443);
                if (tid < 0) {
                    set_color(GFX_LRED, GFX_BLACK);
                    print("certcheck: TCP gagal\n");
                    set_color(GFX_WHITE, GFX_BLACK);
                } else {
                    int rc = tls13_connect(tid, host);
                    if (rc != 0) {
                        set_color(GFX_LRED, GFX_BLACK);
                        print("certcheck: TLS handshake gagal\n");
                        set_color(GFX_WHITE, GFX_BLACK);
                    } else {
                        X509Cert cert;
                        set_color(GFX_LCYAN, GFX_BLACK);
                        print("=== Sertifikat Server ===\n");
                        set_color(GFX_WHITE, GFX_BLACK);
                        if (tls13_get_last_cert(&cert)) {
                            x509_print(&cert);
                            int hn = tls13_check_hostname(host);
                            if (hn == 1) {
                                set_color(GFX_LGREEN, GFX_BLACK);
                                print("  Hostname    : OK (cocok)\n");
                            } else {
                                set_color(GFX_YELLOW, GFX_BLACK);
                                print("  Hostname    : TIDAK COCOK\n");
                            }
                            set_color(GFX_WHITE, GFX_BLACK);
                        } else {
                            print("  [sertifikat tidak tersedia]\n");
                        }
                        tls13_close(tid);
                    }
                    net_tcp_close(tid);
                }
            }
        }
    }
    /* F-X4: nslookup <hostname> */
    else if (str_starts_with(input_buffer, "nslookup ")) {
        const char *host = input_buffer + 9;
        while (*host == ' ') host++;
        if (!*host) {
            print("nslookup: gunakan: nslookup <hostname>\n");
        } else {
            print("nslookup: resolving '");
            print(host); print("'...\n");
            uint8_t ip[4];
            if (dns_resolve(host, ip)) {
                set_color(GFX_LGREEN, GFX_BLACK);
                print("nslookup: ");
                print(host); print(" -> ");
                char buf[6];
                itoa(ip[0], buf); print(buf); print(".");
                itoa(ip[1], buf); print(buf); print(".");
                itoa(ip[2], buf); print(buf); print(".");
                itoa(ip[3], buf); print(buf); print("\n");
                set_color(GFX_WHITE, GFX_BLACK);
            } else {
                set_color(GFX_LRED, GFX_BLACK);
                print("nslookup: gagal resolve '"); print(host); print("'\n");
                set_color(GFX_WHITE, GFX_BLACK);
            }
        }
    }
    /* Fondasi AO: vdisk — VirtIO block device info/read */
    else if (str_compare(input_buffer, "vdisk")) {
        if (!virtio_blk_present()) {
            print("vdisk: VirtIO blk tidak ditemukan\n");
            print("vdisk: tambah -drive file=disk.img,if=virtio,format=raw ke QEMU\n");
        } else {
            uint64_t cap = virtio_blk_capacity();
            uint64_t mb  = (cap * 512) / (1024ULL * 1024ULL);
            set_color(0x0055FF55, 0);
            print("vdisk: VirtIO blk tersedia\n");
            print("  kapasitas : ");
            { char nb[16]; itoa((uint32_t)(cap & 0xFFFFFFFF), nb); print(nb); }
            print(" sektor (");
            { char nb[16]; itoa((uint32_t)mb, nb); print(nb); }
            print(" MB)\n");
            set_color(0x00FFFFFF, 0);
        }
    }
    else if (str_starts_with(input_buffer, "vdisk read ")) {
        const char *p3 = input_buffer + 11;
        while (*p3 == ' ') p3++;
        uint64_t sector = 0;
        while (*p3 >= '0' && *p3 <= '9') sector = sector * 10 + (uint64_t)(*p3++ - '0');
        if (!virtio_blk_present()) {
            print("vdisk: tidak ada device\n");
        } else {
            static uint8_t secbuf[512];
            int r = virtio_blk_read(sector, secbuf, 1);
            if (r < 0) {
                print("vdisk: read error\n");
            } else {
                set_color(0x0055FFFF, 0);
                print("vdisk: sektor "); { char nb[12]; itoa((uint32_t)sector, nb); print(nb); } print(":\n");
                set_color(0x00FFFFFF, 0);
                /* Dump hex 32 byte pertama */
                const char *hex = "0123456789ABCDEF";
                char hline[64]; int hi = 0;
                int bi;
                for (bi = 0; bi < 32; bi++) {
                    hline[hi++] = hex[secbuf[bi] >> 4];
                    hline[hi++] = hex[secbuf[bi] & 0xF];
                    hline[hi++] = ' ';
                    if ((bi % 16) == 15) {
                        hline[hi] = 0; print(hline); print("\n"); hi = 0;
                    }
                }
                if (hi > 0) { hline[hi] = 0; print(hline); print("\n"); }
            }
        }
    }
    else if (str_compare(input_buffer, "ntpdate")) {
        ntp_sync();
    }
    /* --- Fondasi AR: EXT2 filesystem --- */
    else if (str_compare(input_buffer, "mount ext2") || str_starts_with(input_buffer, "mount ext2")) {
        ext2_mount();
    }
    else if (str_compare(input_buffer, "ext2ls") || str_compare(input_buffer, "ext2ls /")) {
        ext2_ls("/");
    }
    else if (str_starts_with(input_buffer, "ext2ls ")) {
        ext2_ls(input_buffer + 7);
    }
    else if (str_starts_with(input_buffer, "ext2cat ")) {
        ext2_cat(input_buffer + 8);
    }
    /* --- Fondasi AZ: dmesg — tampilkan kernel ring buffer log --- */
    else if (str_compare(input_buffer, "dmesg")) {
        char dmbuf[4096];
        int n = dmesg_read(dmbuf, 4096);
        if (n > 0) {
            int i;
            for (i = 0; i < n; i++) print_char(dmbuf[i]);
        } else {
            set_color(GFX_YELLOW, GFX_BLACK);
            print("[dmesg: buffer kosong]\n");
            set_color(GFX_WHITE, GFX_BLACK);
        }
    }
    /* --- Fondasi AS: kdbg — Kernel Debugger via serial --- */
    else if (str_compare(input_buffer, "kdbg")) {
        set_color(GFX_LCYAN, GFX_BLACK);
        print("kdbg: masuk mode debugger serial (COM1).\n");
        print("      Sambungkan terminal serial ke QEMU -serial stdio\n");
        print("      atau gunakan 'kdbg' dari layar utama saat QEMU -serial telnet:...\n");
        set_color(GFX_WHITE, GFX_BLACK);
        kdbg_run();
        set_color(GFX_LGREEN, GFX_BLACK);
        print("kdbg: keluar dari debugger.\n");
        set_color(GFX_WHITE, GFX_BLACK);
    }
    /* Fondasi AM: httpd [start [port] | stop] — HTTP server */
    else if (str_starts_with(input_buffer, "httpd") || str_compare(input_buffer, "httpd")) {
        /* Parse subcommand */
        const char *sub = input_buffer + 5;
        while (*sub == ' ') sub++;
        int do_stop = (sub[0]=='s' && sub[1]=='t' && sub[2]=='o' && sub[3]=='p');
        int port_num = 8080;
        if (!do_stop) {
            /* "start [port]" atau kosong */
            const char *p2 = sub;
            if (p2[0]=='s'&&p2[1]=='t'&&p2[2]=='a'&&p2[3]=='r'&&p2[4]=='t') {
                p2 += 5; while (*p2 == ' ') p2++;
            }
            if (*p2 >= '0' && *p2 <= '9') {
                int pv = 0;
                while (*p2 >= '0' && *p2 <= '9') pv = pv * 10 + (*p2++ - '0');
                port_num = pv;
            }
            set_color(0x0055FF55, 0);
            print("httpd: listen di port "); { char nb[8]; itoa((uint32_t)port_num, nb); print(nb); }
            print(" (ketuk Ctrl+C untuk stop, atau tunggu otomatis 10 koneksi)\n");
            print("httpd: akses dari host: http://localhost:"); { char nb[8]; itoa((uint32_t)port_num, nb); print(nb); }
            print("/\n");
            set_color(0x00FFFFFF, 0);

            int lid = net_tcp_listen((uint16_t)port_num);
            if (lid < 0) { print("httpd: gagal listen\n"); }
            else {
                int req_count = 0;
                while (req_count < 10) {
                    int cid = net_tcp_accept(lid, 5000);
                    if (cid < 0) { print("httpd: timeout, stop\n"); break; }
                    req_count++;

                    /* Baca HTTP request */
                    static uint8_t hbuf[1024];
                    int hlen = net_tcp_recv(cid, hbuf, (uint16_t)(sizeof(hbuf) - 1));
                    if (hlen <= 0) { net_tcp_close(cid); continue; }
                    hbuf[hlen] = 0;

                    /* Cari path: "GET /path HTTP" */
                    static char hpath[128];
                    hpath[0] = 0;
                    {
                        int pi = 0;
                        /* Cari "GET " */
                        uint8_t *g = hbuf;
                        while (*g && !(g[0]=='G'&&g[1]=='E'&&g[2]=='T'&&g[3]==' ')) g++;
                        if (*g) {
                            g += 4; /* skip "GET " */
                            /* copy path sampai spasi */
                            while (*g && *g != ' ' && pi < 127)
                                hpath[pi++] = (char)*g++;
                        }
                        hpath[pi] = 0;
                    }

                    /* Logging */
                    set_color(0x0055FF55, 0);
                    print("httpd: GET ");
                    print(hpath[0] ? hpath : "/");
                    print("\n");
                    set_color(0x00FFFFFF, 0);

                    /* Serve konten */
                    static uint8_t fbuf[8192];
                    uint32_t fsize = 0;
                    const char *content_type = "text/plain";
                    int found = 0;

                    if (!hpath[0] || (hpath[0]=='/'&&!hpath[1])) {
                        /* Root: tampilkan index sederhana */
                        static const char idx[] =
                            "<!DOCTYPE html><html><head><title>Oria OS</title></head>"
                            "<body><h1>Oria OS HTTP Server</h1>"
                            "<p>Fondasi AM - HTTP Server berjalan di Oria OS.</p>"
                            "<p>Gunakan URL /namafile untuk mengakses file dari MFS4.</p>"
                            "</body></html>";
                        int idxl = 0; while (idx[idxl]) idxl++;
                        int ki; for (ki = 0; ki < idxl; ki++) fbuf[ki] = (uint8_t)idx[ki];
                        fsize = (uint32_t)idxl;
                        content_type = "text/html";
                        found = 1;
                    } else {
                        /* Coba baca file dari MFS4 */
                        char fname[64]; int fi = 0;
                        const char *hp = hpath;
                        if (*hp == '/') hp++;
                        while (*hp && fi < 63) fname[fi++] = *hp++;
                        fname[fi] = 0;
                        if (fi > 0) {
                            char fpath[128];
                            make_path(fname, fpath, sizeof(fpath));
                            void *data = fs_read_bin(fpath, &fsize);
                            if (data && fsize > 0 && fsize <= sizeof(fbuf)) {
                                int ki;
                                for (ki = 0; ki < (int)fsize; ki++)
                                    fbuf[ki] = ((uint8_t*)data)[ki];
                                found = 1;
                            }
                        }
                    }

                    /* Bangun response */
                    static char resp_hdr[256];
                    int rhi = 0;
                    const char *status = found ? "200 OK" : "404 Not Found";
                    const char *r0 = "HTTP/1.0 "; int r0i = 0; while (r0[r0i]) resp_hdr[rhi++] = r0[r0i++];
                    int ri; for (ri = 0; status[ri]; ri++) resp_hdr[rhi++] = status[ri];
                    resp_hdr[rhi++] = '\r'; resp_hdr[rhi++] = '\n';
                    const char *ct = "Content-Type: "; int cti = 0; while (ct[cti]) resp_hdr[rhi++] = ct[cti++];
                    for (ri = 0; content_type[ri]; ri++) resp_hdr[rhi++] = content_type[ri];
                    resp_hdr[rhi++] = '\r'; resp_hdr[rhi++] = '\n';
                    const char *cl = "Content-Length: "; int cli = 0; while (cl[cli]) resp_hdr[rhi++] = cl[cli++];
                    { char nb[12]; itoa(found ? fsize : 9, nb); int ni = 0; while (nb[ni]) resp_hdr[rhi++] = nb[ni++]; }
                    resp_hdr[rhi++] = '\r'; resp_hdr[rhi++] = '\n';
                    const char *conn = "Connection: close\r\n\r\n"; int coni = 0; while (conn[coni]) resp_hdr[rhi++] = conn[coni++];

                    net_tcp_send(cid, resp_hdr, (uint16_t)rhi);
                    if (found) {
                        net_tcp_send(cid, fbuf, (uint16_t)fsize);
                    } else {
                        net_tcp_send(cid, "Not Found", 9);
                    }
                    net_tcp_close(cid);
                }
                net_tcp_unlisten(lid);
                print("httpd: selesai\n");
            }
        }
    }
    /* Fondasi AN: ws <url> — WebSocket client */
    else if (str_starts_with(input_buffer, "ws ")) {
        const char *wsurl = input_buffer + 3;
        while (*wsurl == ' ') wsurl++;
        if (!*wsurl) {
            print("ws: gunakan: ws ws://host/path\n");
        } else {
            ws_connect(wsurl);
        }
    }
    else if (str_compare(input_buffer, "cpuinfo")) {        char nbuf[16];
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
    /* ================================================================
     * FONDASI AB — Alat Unix bawaan (cat, cp, mv, wc, head, tail,
     *               find, stat, touch)
     * ================================================================ */
    /* cat <file> */
    else if (str_compare(input_buffer, "cat")) {
        print("Penggunaan: cat <file>\n");
    }
    else if (str_starts_with(input_buffer, "cat ")) {
        const char *fn = input_buffer + 4;
        while (*fn == ' ') fn++;
        char pbuf[64];
        fn = make_path(fn, pbuf, 64);
        uint32_t sz;
        const uint8_t *d = fs_read_bin(fn, &sz);
        if (!d) {
            set_color(GFX_LRED, GFX_BLACK);
            print("cat: tidak ditemukan: "); print(fn); print("\n");
            set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
        } else {
            uint32_t i;
            for (i = 0; i < sz; i++) {
                char ch = (char)d[i];
                if (ch == '\r') continue;
                print_char(ch);
            }
            if (sz > 0 && (char)d[sz-1] != '\n') print_char('\n');
        }
    }
    /* cp <sumber> <tujuan> */
    else if (str_compare(input_buffer, "cp")) {
        print("Penggunaan: cp <sumber> <tujuan>\n");
    }
    else if (str_starts_with(input_buffer, "cp ")) {
        const char *args = input_buffer + 3;
        int sp = str_find_space(args);
        if (sp < 0) { print("Penggunaan: cp <sumber> <tujuan>\n"); last_exit_code = 1; }
        else {
            char srcb[64], dstb[64], sp2[64], dp2[64]; int ii;
            for (ii = 0; ii < sp && ii < 63; ii++) srcb[ii] = args[ii];
            srcb[sp < 63 ? sp : 63] = '\0';
            const char *drest = args + sp + 1;
            for (ii = 0; drest[ii] && ii < 63; ii++) dstb[ii] = drest[ii]; dstb[ii] = '\0';
            const char *srcf = make_path(srcb, sp2, 64);
            const char *dstf = make_path(dstb, dp2, 64);
            uint32_t sz2; const uint8_t *d2 = fs_read_bin(srcf, &sz2);
            if (!d2) {
                set_color(GFX_LRED, GFX_BLACK);
                print("cp: tidak ditemukan: "); print(srcf); print("\n");
                set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
            } else if (!fs_write_bin(dstf, d2, sz2)) {
                set_color(GFX_LRED, GFX_BLACK);
                print("cp: gagal menulis ke: "); print(dstf); print("\n");
                set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
            } else {
                set_color(GFX_LGREEN, GFX_BLACK);
                print("cp: "); print(srcf); print(" -> "); print(dstf); print("\n");
                set_color(GFX_WHITE, GFX_BLACK);
            }
        }
    }
    /* mv <lama> <baru> */
    else if (str_compare(input_buffer, "mv")) {
        print("Penggunaan: mv <lama> <baru>\n");
    }
    else if (str_starts_with(input_buffer, "mv ")) {
        const char *args = input_buffer + 3;
        int sp = str_find_space(args);
        if (sp < 0) { print("Penggunaan: mv <lama> <baru>\n"); last_exit_code = 1; }
        else {
            char oldb[64], newb[64], op[64], np[64]; int ii;
            for (ii = 0; ii < sp && ii < 63; ii++) oldb[ii] = args[ii];
            oldb[sp < 63 ? sp : 63] = '\0';
            const char *drest = args + sp + 1;
            for (ii = 0; drest[ii] && ii < 63; ii++) newb[ii] = drest[ii]; newb[ii] = '\0';
            int rc = mfs4_rename(make_path(oldb, op, 64), make_path(newb, np, 64));
            if (rc == 0) {
                set_color(GFX_LGREEN, GFX_BLACK);
                print("mv: "); print(make_path(oldb, op, 64));
                print(" -> "); print(make_path(newb, np, 64)); print("\n");
                set_color(GFX_WHITE, GFX_BLACK);
            } else {
                set_color(GFX_LRED, GFX_BLACK);
                print("mv: gagal (file tidak ada atau nama sudah digunakan)\n");
                set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
            }
        }
    }
    /* wc <file> */
    else if (str_compare(input_buffer, "wc")) {
        print("Penggunaan: wc <file>\n");
    }
    else if (str_starts_with(input_buffer, "wc ")) {
        const char *fn = input_buffer + 3;
        while (*fn == ' ') fn++;
        char pbuf[64]; fn = make_path(fn, pbuf, 64);
        uint32_t sz;
        const uint8_t *d = fs_read_bin(fn, &sz);
        if (!d) {
            set_color(GFX_LRED, GFX_BLACK);
            print("wc: tidak ditemukan: "); print(fn); print("\n");
            set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
        } else {
            uint32_t i, lines = 0, words = 0; int in_word = 0;
            for (i = 0; i < sz; i++) {
                char ch = (char)d[i];
                if (ch == '\n') lines++;
                if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') in_word = 0;
                else { if (!in_word) { words++; in_word = 1; } }
            }
            char nb[12];
            itoa(lines, nb); print(nb); print(" ");
            itoa(words, nb); print(nb); print(" ");
            itoa(sz,    nb); print(nb); print(" ");
            print(fn); print("\n");
        }
    }
    /* head [-n N] <file> */
    else if (str_compare(input_buffer, "head")) {
        print("Penggunaan: head [-n N] <file>  (default 10 baris)\n");
    }
    else if (str_starts_with(input_buffer, "head ")) {
        const char *p = input_buffer + 5; int nlns = 10;
        while (*p == ' ') p++;
        if (p[0] == '-' && p[1] == 'n' && p[2] == ' ') {
            p += 3; while (*p == ' ') p++;
            nlns = 0;
            while (*p >= '0' && *p <= '9') { nlns = nlns * 10 + (*p - '0'); p++; }
            while (*p == ' ') p++;
        }
        char pbuf[64]; p = make_path(p, pbuf, 64);
        uint32_t sz; const uint8_t *d = fs_read_bin(p, &sz);
        if (!d) {
            set_color(GFX_LRED, GFX_BLACK);
            print("head: tidak ditemukan: "); print(p); print("\n");
            set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
        } else {
            int cur = 0; uint32_t i;
            for (i = 0; i < sz && cur < nlns; i++) {
                char ch = (char)d[i]; if (ch == '\r') continue;
                print_char(ch); if (ch == '\n') cur++;
            }
        }
    }
    /* tail [-n N] <file> */
    else if (str_compare(input_buffer, "tail")) {
        print("Penggunaan: tail [-n N] <file>  (default 10 baris)\n");
    }
    else if (str_starts_with(input_buffer, "tail ")) {
        const char *p = input_buffer + 5; int nlns = 10;
        while (*p == ' ') p++;
        if (p[0] == '-' && p[1] == 'n' && p[2] == ' ') {
            p += 3; while (*p == ' ') p++;
            nlns = 0;
            while (*p >= '0' && *p <= '9') { nlns = nlns * 10 + (*p - '0'); p++; }
            while (*p == ' ') p++;
        }
        char pbuf[64]; p = make_path(p, pbuf, 64);
        uint32_t sz; const uint8_t *d = fs_read_bin(p, &sz);
        if (!d) {
            set_color(GFX_LRED, GFX_BLACK);
            print("tail: tidak ditemukan: "); print(p); print("\n");
            set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
        } else {
            uint32_t i; int total_nl = 0;
            for (i = 0; i < sz; i++) if ((char)d[i] == '\n') total_nl++;
            int skip = total_nl - nlns; if (skip < 0) skip = 0;
            int skipped = 0;
            for (i = 0; i < sz; i++) {
                char ch = (char)d[i]; if (ch == '\r') continue;
                if (skipped < skip) { if (ch == '\n') skipped++; continue; }
                print_char(ch);
            }
        }
    }
    /* find <pola> */
    else if (str_compare(input_buffer, "find")) {
        print("Penggunaan: find <pola>  (cari file yang mengandung pola)\n");
    }
    else if (str_starts_with(input_buffer, "find ")) {
        const char *pat = input_buffer + 5;
        while (*pat == ' ') pat++;
        static char find_list[2048];
        int n = fs_list_buf(find_list, 2048);
        if (n <= 0) {
            set_color(GFX_LGRAY, GFX_BLACK); print("find: filesystem kosong\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else {
            int fi = 0, found = 0;
            while (find_list[fi]) {
                char name[64]; int ni = 0;
                while (find_list[fi] && find_list[fi] != '\n' && ni < 63)
                    name[ni++] = find_list[fi++];
                name[ni] = '\0';
                if (find_list[fi] == '\n') fi++;
                if (ni == 0) continue;
                int pi, nn, contains = 0;
                for (nn = 0; name[nn] && !contains; nn++) {
                    int match = 1; pi = 0;
                    while (pat[pi] && name[nn+pi]) {
                        if (pat[pi] != name[nn+pi]) { match = 0; break; }
                        pi++;
                    }
                    if (match && pat[pi] == '\0') contains = 1;
                }
                if (contains) { print(name); print("\n"); found++; }
            }
            if (!found) {
                set_color(GFX_LGRAY, GFX_BLACK);
                print("find: tidak ada hasil untuk: "); print(pat); print("\n");
                set_color(GFX_WHITE, GFX_BLACK);
            }
        }
    }
    /* stat <file> */
    else if (str_compare(input_buffer, "stat")) {
        print("Penggunaan: stat <file>\n");
    }
    else if (str_starts_with(input_buffer, "stat ")) {
        const char *fn = input_buffer + 5;
        while (*fn == ' ') fn++;
        char pbuf[64]; fn = make_path(fn, pbuf, 64);
        MFS4Stat st;
        if (mfs4_stat(fn, &st) != 0) {
            set_color(GFX_LRED, GFX_BLACK);
            print("stat: tidak ditemukan: "); print(fn); print("\n");
            set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
        } else {
            char nb[12];
            print("  Nama  : "); print(fn); print("\n");
            print("  Tipe  : ");
            if (st.type == MFS4_TYPE_DIR) print("direktori\n"); else print("file\n");
            print("  Ukuran: "); itoa(st.size, nb); print(nb); print(" byte\n");
            { /* perms sebagai hex */
              char hx[5]; int hxi = 0;
              uint16_t pv = (uint16_t)st.perms;
              const char hxd[] = "0123456789ABCDEF";
              hx[hxi++] = hxd[(pv >> 12) & 0xF]; hx[hxi++] = hxd[(pv >>  8) & 0xF];
              hx[hxi++] = hxd[(pv >>  4) & 0xF]; hx[hxi++] = hxd[(pv      ) & 0xF];
              hx[hxi]   = '\0';
              print("  Perms : 0x"); print(hx); print("\n");
            }
            print("  mtime : "); itoa(st.mtime, nb); print(nb); print(" ticks\n");
        }
    }
    /* touch <file> */
    else if (str_compare(input_buffer, "touch")) {
        print("Penggunaan: touch <file>\n");
    }
    else if (str_starts_with(input_buffer, "touch ")) {
        const char *fn = input_buffer + 6;
        while (*fn == ' ') fn++;
        char pbuf[64]; fn = make_path(fn, pbuf, 64);
        uint32_t sz2; const uint8_t *ex = fs_read_bin(fn, &sz2);
        if (ex) {
            set_color(GFX_LGRAY, GFX_BLACK);
            print("touch: "); print(fn); print(" (sudah ada, tidak berubah)\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else {
            if (fs_write(fn, "")) {
                set_color(GFX_LGREEN, GFX_BLACK);
                print("touch: "); print(fn); print(" dibuat\n");
                set_color(GFX_WHITE, GFX_BLACK);
            } else {
                set_color(GFX_LRED, GFX_BLACK);
                print("touch: gagal membuat file\n");
                set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
            }
        }
    }
    /* ================================================================
     * FONDASI AE — Text Editor: edit <file>
     * ================================================================ */
    else if (str_compare(input_buffer, "edit")) {
        print("Penggunaan: edit <file>\n");
    }
    else if (str_starts_with(input_buffer, "edit ")) {
        const char *fn = input_buffer + 5;
        while (*fn == ' ') fn++;
        char pbuf[64]; fn = make_path(fn, pbuf, 64);
        ed_load(fn);
        editor_active = 1;
        ed_render_all();
    }
    /* ================================================================
     * Fondasi AY — User Accounts: login, whoami, passwd, adduser, su
     * Format /etc/passwd: username:hash32:uid:home\n
     * hash32: djb2-32 dari password, ditulis desimal.
     * ================================================================ */
    else if (str_compare(input_buffer, "whoami")) {
        int tid = task_get_current();
        int uid = task_get_uid(tid);
        /* Cari username di /etc/passwd berdasar uid */
        const char *data = fs_read("etc/passwd");
        if (data && uid >= 0) {
            /* Cari baris dengan uid cocok */
            const char *p = data;
            int found = 0;
            while (*p) {
                /* parse: user:hash:uid:home */
                char uname[32]; int ui = 0;
                while (*p && *p != ':' && ui < 31) uname[ui++] = *p++;
                uname[ui] = '\0';
                if (*p == ':') p++;  /* skip hash */
                while (*p && *p != ':') p++;
                if (*p == ':') p++;
                /* parse uid field */
                int fuid = 0;
                while (*p >= '0' && *p <= '9') { fuid = fuid * 10 + (*p - '0'); p++; }
                while (*p && *p != '\n') p++;  /* skip rest */
                if (*p == '\n') p++;
                if (fuid == uid) {
                    print(uname); print(" (uid=");
                    char nb[8]; itoa((uint32_t)uid, nb); print(nb); print(")\n");
                    found = 1; break;
                }
            }
            if (!found) { print("root (uid=0)\n"); }
        } else {
            print("root (uid=0)\n");
        }
    }
    else if (str_compare(input_buffer, "login")) {
        /* Prompt username dan password, verifikasi, set uid */
        set_color(GFX_LGRAY, GFX_BLACK);
        print("Username: ");
        char luser[32]; int li = 0;
        /* Blocking input loop */
        while (1) {
            char c = keyboard_getchar_block();
            if (c == '\n') break;
            if (c == '\b') { if (li > 0) { li--; backspace_char(); } continue; }
            if (li < 31) { luser[li++] = c; print_char(c); }
        }
        luser[li] = '\0'; print("\n");
        print("Password: ");
        char lpass[64]; int lpi = 0;
        while (1) {
            char c = keyboard_getchar_block();
            if (c == '\n') break;
            if (c == '\b') { if (lpi > 0) lpi--; continue; }
            if (lpi < 63) lpass[lpi++] = c;
        }
        lpass[lpi] = '\0'; print("\n");
        /* djb2-32 hash */
        uint32_t hash = 5381;
        int hi;
        for (hi = 0; lpass[hi]; hi++)
            hash = ((hash << 5) + hash) + (uint8_t)lpass[hi];
        /* Cari di /etc/passwd */
        const char *data = fs_read("etc/passwd");
        int login_ok = 0;
        int found_uid = 0;
        if (data) {
            const char *p = data;
            while (*p) {
                char uname[32]; int ui2 = 0;
                while (*p && *p != ':' && ui2 < 31) uname[ui2++] = *p++;
                uname[ui2] = '\0';
                if (*p == ':') p++;
                /* parse hash */
                uint32_t fhash = 0;
                while (*p >= '0' && *p <= '9') { fhash = fhash * 10 + (uint32_t)(*p - '0'); p++; }
                if (*p == ':') p++;
                /* parse uid */
                int fuid2 = 0;
                while (*p >= '0' && *p <= '9') { fuid2 = fuid2 * 10 + (*p - '0'); p++; }
                while (*p && *p != '\n') p++;
                if (*p == '\n') p++;
                if (str_compare(uname, luser) && fhash == hash) {
                    login_ok = 1; found_uid = fuid2; break;
                }
            }
        }
        if (login_ok) {
            task_set_uid(task_get_current(), found_uid);
            set_color(GFX_LGREEN, GFX_BLACK);
            print("Login berhasil sebagai "); print(luser); print("\n");
            set_color(GFX_WHITE, GFX_BLACK);
        } else {
            set_color(GFX_LRED, GFX_BLACK);
            print("Login gagal: username atau password salah\n");
            set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
        }
    }
    else if (str_starts_with(input_buffer, "adduser ")) {
        int tid = task_get_current();
        if (task_get_uid(tid) != 0) {
            set_color(GFX_LRED, GFX_BLACK);
            print("adduser: perlu hak root (uid=0)\n");
            set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
        } else {
            /* adduser <username> <password> [uid] */
            const char *p = input_buffer + 8;
            char auname[32]; int ai = 0;
            while (*p && *p != ' ' && ai < 31) auname[ai++] = *p++;
            auname[ai] = '\0';
            while (*p == ' ') p++;
            char apass[64]; int api = 0;
            while (*p && *p != ' ' && api < 63) apass[api++] = *p++;
            apass[api] = '\0';
            while (*p == ' ') p++;
            int auid = 1000;
            if (*p >= '0' && *p <= '9') {
                auid = 0;
                while (*p >= '0' && *p <= '9') { auid = auid * 10 + (*p - '0'); p++; }
            }
            /* hash password */
            uint32_t ahash = 5381;
            int ahi;
            for (ahi = 0; apass[ahi]; ahi++)
                ahash = ((ahash << 5) + ahash) + (uint8_t)apass[ahi];
            /* Pastikan etc/passwd ada, baca isi lama */
            const char *existing = fs_read("etc/passwd");
            char newbuf[1024]; int nb2i = 0;
            if (existing) {
                const char *ex = existing;
                while (*ex && nb2i < 900) newbuf[nb2i++] = *ex++;
            }
            /* Tambahkan baris baru: uname:hash:uid:home\n */
            const char *np = auname;
            while (*np && nb2i < 1020) newbuf[nb2i++] = *np++;
            newbuf[nb2i++] = ':';
            char hbuf[12]; itoa(ahash, hbuf);
            const char *hn = hbuf;
            while (*hn && nb2i < 1020) newbuf[nb2i++] = *hn++;
            newbuf[nb2i++] = ':';
            char ubuf[8]; itoa((uint32_t)auid, ubuf);
            const char *un = ubuf;
            while (*un && nb2i < 1020) newbuf[nb2i++] = *un++;
            newbuf[nb2i++] = ':';
            newbuf[nb2i++] = '/'; newbuf[nb2i++] = 'h'; newbuf[nb2i++] = 'o'; newbuf[nb2i++] = 'm'; newbuf[nb2i++] = 'e';
            newbuf[nb2i++] = '\n'; newbuf[nb2i] = '\0';
            if (fs_write("etc/passwd", newbuf)) {
                set_color(GFX_LGREEN, GFX_BLACK);
                print("adduser: pengguna "); print(auname); print(" dibuat (uid=");
                char ub2[8]; itoa((uint32_t)auid, ub2); print(ub2); print(")\n");
                set_color(GFX_WHITE, GFX_BLACK);
            } else {
                set_color(GFX_LRED, GFX_BLACK);
                print("adduser: gagal menulis etc/passwd\n");
                set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
            }
        }
    }
    else if (str_compare(input_buffer, "passwd")) {
        set_color(GFX_LGRAY, GFX_BLACK);
        print("Password baru: ");
        char npass[64]; int npi = 0;
        while (1) {
            char c = keyboard_getchar_block();
            if (c == '\n') break;
            if (c == '\b') { if (npi > 0) npi--; continue; }
            if (npi < 63) npass[npi++] = c;
        }
        npass[npi] = '\0'; print("\n");
        int cur_uid = task_get_uid(task_get_current());
        uint32_t nhash = 5381;
        int nhi;
        for (nhi = 0; npass[nhi]; nhi++)
            nhash = ((nhash << 5) + nhash) + (uint8_t)npass[nhi];
        /* Baca /etc/passwd, update baris uid-nya */
        const char *data = fs_read("etc/passwd");
        if (!data) {
            set_color(GFX_LRED, GFX_BLACK);
            print("passwd: tidak dapat membaca etc/passwd\n");
            set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
        } else {
            char outbuf[1024]; int obi = 0;
            const char *p = data; int updated = 0;
            while (*p && obi < 950) {
                /* Baca satu baris */
                char line[128]; int li2 = 0;
                while (*p && *p != '\n' && li2 < 127) line[li2++] = *p++;
                line[li2] = '\0';
                if (*p == '\n') p++;
                /* Parse uid field (posisi ke-3 setelah 2 ':') */
                char *lp = line; int colon = 0;
                char *uid_start = 0;
                char *uname_start = line;
                while (*lp) { if (*lp == ':') { colon++; if (colon == 2) uid_start = lp+1; } lp++; }
                int line_uid = -1;
                if (uid_start) {
                    line_uid = 0;
                    char *up = uid_start;
                    while (*up >= '0' && *up <= '9') { line_uid = line_uid * 10 + (*up - '0'); up++; }
                }
                if (!updated && line_uid == cur_uid) {
                    /* Bangun ulang baris dengan hash baru */
                    /* Tulis username */
                    char *wp = line;
                    while (*wp && *wp != ':' && obi < 950) outbuf[obi++] = *wp++;
                    if (*wp == ':') { outbuf[obi++] = ':'; wp++; }
                    /* Skip hash lama, tulis hash baru */
                    while (*wp && *wp != ':') wp++;
                    char nhbuf[12]; itoa(nhash, nhbuf);
                    const char *nhn = nhbuf;
                    while (*nhn && obi < 950) outbuf[obi++] = *nhn++;
                    /* Salin sisa (uid:home) */
                    while (*wp && obi < 950) outbuf[obi++] = *wp++;
                    outbuf[obi++] = '\n'; updated = 1;
                } else {
                    /* Salin apa adanya */
                    char *wp = line;
                    while (*wp && obi < 950) outbuf[obi++] = *wp++;
                    outbuf[obi++] = '\n';
                }
            }
            outbuf[obi] = '\0';
            if (updated && fs_write("etc/passwd", outbuf)) {
                set_color(GFX_LGREEN, GFX_BLACK);
                print("passwd: password berhasil diubah\n");
                set_color(GFX_WHITE, GFX_BLACK);
            } else {
                set_color(GFX_LRED, GFX_BLACK);
                print("passwd: gagal menyimpan\n");
                set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
            }
        }
    }
    else if (str_starts_with(input_buffer, "su ")) {
        /* su <username>: ganti user (hanya root yang bisa, atau ke dirinya sendiri) */
        const char *su_user = input_buffer + 3;
        while (*su_user == ' ') su_user++;
        int cur_uid = task_get_uid(task_get_current());
        const char *data = fs_read("etc/passwd");
        int su_uid = -1;
        if (data) {
            const char *p = data;
            while (*p) {
                char uname[32]; int ui = 0;
                while (*p && *p != ':' && ui < 31) uname[ui++] = *p++;
                uname[ui] = '\0';
                /* skip hash */
                while (*p && *p != ':') p++;
                if (*p == ':') p++;
                /* parse uid */
                int fuid3 = 0;
                while (*p >= '0' && *p <= '9') { fuid3 = fuid3 * 10 + (*p - '0'); p++; }
                while (*p && *p != '\n') p++;
                if (*p == '\n') p++;
                if (str_compare(uname, su_user)) { su_uid = fuid3; break; }
            }
        }
        if (su_uid < 0) {
            set_color(GFX_LRED, GFX_BLACK);
            print("su: pengguna tidak ditemukan\n");
            set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
        } else if (cur_uid != 0 && cur_uid != su_uid) {
            set_color(GFX_LRED, GFX_BLACK);
            print("su: perlu hak root untuk berpindah pengguna\n");
            set_color(GFX_WHITE, GFX_BLACK); last_exit_code = 1;
        } else {
            task_set_uid(task_get_current(), su_uid);
            set_color(GFX_LGREEN, GFX_BLACK);
            print("su: sekarang sebagai "); print(su_user); print("\n");
            set_color(GFX_WHITE, GFX_BLACK);
        }
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
            /* Sintaks: prog1 [args] | prog2 [args] */
            char prog1[32], prog2[32];
            char tok1b[128], tok2b[128];
            const char *avL[9], *avR[9];
            int acL = 0, acR = 0;
            int j;
            /* Kiri: input_buffer[0..pipe_pos-1] (sebelum ' | ') */
            j = 0; while (j < pipe_pos && j < 127) { tok1b[j] = input_buffer[j]; j++; }
            while (j > 0 && tok1b[j-1] == ' ') j--;
            tok1b[j] = '\0';
            { char *p = tok1b; while (*p == ' ') p++;
              while (*p && acL < 8) { avL[acL++] = p; while (*p && *p != ' ') p++;
                  if (*p) { *p = '\0'; p++; while (*p == ' ') p++; } } avL[acL] = 0; }
            if (acL > 0) { j = 0; const char *s = avL[0]; while (s[j] && j < 31) { prog1[j]=s[j]; j++; } prog1[j]='\0'; }
            else prog1[0] = '\0';
            /* Kanan: setelah ' | ' (pipe_pos + 3) */
            const char *r2 = input_buffer + pipe_pos + 3;
            j = 0; while (r2[j] && j < 127) { tok2b[j] = r2[j]; j++; }
            while (j > 0 && tok2b[j-1] == ' ') j--;
            tok2b[j] = '\0';
            { char *p = tok2b; while (*p == ' ') p++;
              while (*p && acR < 8) { avR[acR++] = p; while (*p && *p != ' ') p++;
                  if (*p) { *p = '\0'; p++; while (*p == ' ') p++; } } avR[acR] = 0; }
            if (acR > 0) { j = 0; const char *s = avR[0]; while (s[j] && j < 31) { prog2[j]=s[j]; j++; } prog2[j]='\0'; }
            else prog2[0] = '\0';

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
                        int tid1 = task_create_user(entry1, dir1, 0x600000 + PAGE_SIZE, prog1, acL, avL);
                        task_set_pipe(tid1, pipe_fd);
                    }
                    uint64_t *dir2 = vmm_create_page_dir();
                    uint64_t entry2 = elf_load(d2, sz2, dir2);
                    if (entry2) {
                        uint64_t sp2 = pmm_alloc_frame();
                        vmm_map_page(dir2, 0x600000, sp2, 7);
                        int tid2 = task_create_user(entry2, dir2, 0x600000 + PAGE_SIZE, prog2, acR, avR);
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
            /* Auto-exec: coba jalankan input_buffer sebagai program.
             * Mendukung: progname [args] [> file] [>> file] [< file] [&]
             * Jika program ditemukan di filesystem, run tanpa 'exec ' prefix. */
            ShCmd ae;
            shcmd_parse(input_buffer, &ae);
            if (ae.argc > 0) {
                int bg = ae.bg || bg_exec;
                ae.bg = 0;
                int ae_tid = shcmd_run_nowait(&ae, -1, -1);
                if (ae_tid == -2) {
                    set_color(GFX_LRED, GFX_BLACK);
                    print("Perintah tidak dikenal: ");
                    print(input_buffer); print("\n");
                    set_color(GFX_WHITE, GFX_BLACK);
                    last_exit_code = 127;
                } else if (ae_tid >= 0) {
                    if (bg) {
                        char tbuf[8]; itoa((uint32_t)ae_tid, tbuf);
                        print("["); print(tbuf); print("] "); print(ae.prog); print(" &\n");
                    } else {
                        keyboard_set_fg_pid(ae_tid);
                        task_wait(ae_tid);
                        last_exit_code = task_get_exit_code(ae_tid);
                        keyboard_set_fg_pid(-1);
                    }
                }
            }
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
    /* Fondasi AE: Saat editor aktif, alihkan semua input ke editor */
    if (editor_active) { ed_process_char(c); return; }
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
    else if (c == '\x19') {                 /* Ctrl+Y: salin baris input ke clipboard */
        if (input_len > 0) {
            input_buffer[input_len] = '\0';
            clip_copy(input_buffer);
            set_color(GFX_LCYAN, GFX_BLACK);
            print(" [disalin]");
            set_color(GFX_WHITE, GFX_BLACK);
        }
    }
    else if (c == '\x16') {                 /* Ctrl+V: tempel dari clipboard */
        char pbuf[256];
        int plen = clip_paste(pbuf, 256);
        int i;
        for (i = 0; i < plen && input_len < 255; i++) {
            char pc = pbuf[i];
            if (pc < ' ') continue;          /* skip karakter kontrol */
            input_buffer[input_len++] = pc;
            print_char(pc);
        }
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