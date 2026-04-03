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
#include "keyboard.h"
#include "mfs4.h"
#include "rtc.h"

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
#define HISTORY_SIZE 32
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
    "help", "clear", "about", "memtest", "uptime", "date",
    "time", "reboot", "ls", "paging", "ps",
    "echo ", "exec ", "read ", "write ", "del ", "rename ", "kill ",
    "cd ", "pwd", "export ", "env",
    "sync", "mkdir ", "chmod ",
    "ifconfig", "ping ", "cpuinfo",
    "udp_send ", "tcp_get ", "nslookup ", "curl ",
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

static void shell_execute(){
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

    /* ---- Pipeline operator '|': prog1 | prog2 ---- */
    {
        int pi = -1, ic;
        for (ic = 0; input_buffer[ic]; ic++) {
            if (input_buffer[ic] == '|') { pi = ic; break; }
        }
        if (pi >= 0) {
            char prog1[32], prog2[32];
            char tok1[128], tok2[128];   /* backing storage untuk tokenisasi argv */
            const char *av1[9], *av2[9]; /* argv arrays */
            int ac1 = 0, ac2 = 0;
            int j;
            /* Kiri: semua token sebelah kiri '|' (strip "exec " bila ada) */
            const char *L = input_buffer;
            while (*L == ' ') L++;
            if (L[0]=='e'&&L[1]=='x'&&L[2]=='e'&&L[3]=='c'&&L[4]==' ') L += 5;
            while (*L == ' ') L++;
            /* Salin ke tok1 sampai '|' */
            j = 0;
            while (L[j] && L[j] != '|' && j < 127) { tok1[j] = L[j]; j++; }
            while (j > 0 && tok1[j-1] == ' ') j--;  /* trim trailing space */
            tok1[j] = '\0';
            /* Tokenize tok1 */
            { char *p = tok1; while (*p == ' ') p++;
              while (*p && ac1 < 8) {
                  av1[ac1++] = p;
                  while (*p && *p != ' ') p++;
                  if (*p) { *p = '\0'; p++; while (*p == ' ') p++; }
              } av1[ac1] = 0; }
            if (ac1 > 0) { j = 0; const char *s = av1[0]; while (s[j] && j < 31) { prog1[j]=s[j]; j++; } prog1[j]='\0'; }
            else prog1[0] = '\0';

            /* Kanan: semua token sebelah kanan '|' (strip "exec " bila ada) */
            const char *R = input_buffer + pi + 1;
            while (*R == ' ') R++;
            if (R[0]=='e'&&R[1]=='x'&&R[2]=='e'&&R[3]=='c'&&R[4]==' ') R += 5;
            while (*R == ' ') R++;
            j = 0;
            while (R[j] && j < 127) { tok2[j] = R[j]; j++; }
            while (j > 0 && tok2[j-1] == ' ') j--;
            tok2[j] = '\0';
            /* Tokenize tok2 */
            { char *p = tok2; while (*p == ' ') p++;
              while (*p && ac2 < 8) {
                  av2[ac2++] = p;
                  while (*p && *p != ' ') p++;
                  if (*p) { *p = '\0'; p++; while (*p == ' ') p++; }
              } av2[ac2] = 0; }
            if (ac2 > 0) { j = 0; const char *s = av2[0]; while (s[j] && j < 31) { prog2[j]=s[j]; j++; } prog2[j]='\0'; }
            else prog2[0] = '\0';

            if (!prog1[0] || !prog2[0]) {
                set_color(GFX_LRED, GFX_BLACK);
                print("pipe: gunakan: prog1 | prog2\n");
                set_color(GFX_WHITE, GFX_BLACK);
                return;
            }
            int pipe_fd = pipe_alloc();
            if (pipe_fd < 0) {
                set_color(GFX_LRED, GFX_BLACK);
                print("pipe: gagal alokasi pipe\n");
                set_color(GFX_WHITE, GFX_BLACK);
                return;
            }
            uint32_t sz1, sz2;
            const uint8_t *d1 = fs_read_bin(prog1, &sz1);
            const uint8_t *d2 = fs_read_bin(prog2, &sz2);
            if (!d1 || !d2) {
                pipe_free(pipe_fd);
                set_color(GFX_LRED, GFX_BLACK);
                if (!d1) { print("pipe: tidak ditemukan: "); print(prog1); print("\n"); }
                if (!d2) { print("pipe: tidak ditemukan: "); print(prog2); print("\n"); }
                set_color(GFX_WHITE, GFX_BLACK);
                return;
            }
            /* Buat prog1 (writer): stdout → pipe write-end */
            uint64_t *dir1 = vmm_create_page_dir();
            uint64_t entry1 = elf_load(d1, sz1, dir1);
            int tid1 = -1;
            if (entry1) {
                uint64_t sp1 = pmm_alloc_frame();
                vmm_map_page(dir1, 0x600000, sp1, 7);
                __asm__ volatile("cli" ::: "memory");
                tid1 = task_create_user(entry1, dir1, 0x600000 + PAGE_SIZE, prog1, ac1, av1);
                vfs_redirect_out_pipe(tid1, pipe_fd);
                __asm__ volatile("sti" ::: "memory");
            }
            /* Buat prog2 (reader): stdin ← pipe read-end */
            uint64_t *dir2 = vmm_create_page_dir();
            uint64_t entry2 = elf_load(d2, sz2, dir2);
            int tid2 = -1;
            if (entry2) {
                uint64_t sp2 = pmm_alloc_frame();
                vmm_map_page(dir2, 0x600000, sp2, 7);
                __asm__ volatile("cli" ::: "memory");
                tid2 = task_create_user(entry2, dir2, 0x600000 + PAGE_SIZE, prog2, ac2, av2);
                vfs_redirect_in_pipe(tid2, pipe_fd);
                __asm__ volatile("sti" ::: "memory");
            }
            if (tid1 < 0 || tid2 < 0) {
                pipe_free(pipe_fd);
                set_color(GFX_LRED, GFX_BLACK);
                print("pipe: gagal memuat ELF\n");
                set_color(GFX_WHITE, GFX_BLACK);
                return;
            }
            /* Tunggu prog1 selesai (EOF terkirim ke prog2), lalu tunggu prog2.
             * F-T: Ctrl+C diteruskan ke prog1 (writer). */
            keyboard_set_fg_pid(tid1);
            task_wait(tid1);
            task_wait(tid2);
            keyboard_set_fg_pid(-1);
            return;
        }
    }

    if(str_compare(input_buffer, "help")){
        set_color(GFX_YELLOW, GFX_BLACK);
        print("Perintah yang tersedia:\n");
        set_color(GFX_WHITE, GFX_BLACK);
        print("help                 - tampilkan daftar perintah\n");
        print("clear                - bersihkan layar\n");
        print("about                - informasi tentang Oria OS\n");
        print("memtest              - test alokasi memory\n");
        print("uptime               - tampilkan waktu berjalan OS\n");
        print("date                 - tampilkan tanggal dan jam (RTC)\n");
        print("echo <text>          - tampilkan text\n");
        print("time                 - tampilkan ticks sejak boot\n");
        print("reboot               - reboot sistem\n");
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
        print("exec <nama> [&]      - jalankan program ELF (& = background)\n");
        print("exec <nama> > <file> - jalankan program, stdout ke file\n");
        print("exec <nama> < <file> - jalankan program, stdin dari file\n");
        print("ps                   - tampilkan daftar proses\n");
        print("kill <id>            - matikan proses berdasarkan ID\n");
        print("setprio <id> <1-3>   - ubah priority proses\n");
    }
    else if(str_compare(input_buffer, "clear")){
        clear_screen();
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
        /* Parse: exec <prog> [arg1 arg2 ...] [< infile] [> outfile] [&] */
        char exec_name[64];
        char rout_file[32];
        char rin_file[32];
        char cmd_buf[128];         /* backing store untuk tokenisasi argumen */
        const char *exec_argv[9];  /* argv[0..7] + NULL sentinel */
        int  exec_argc = 0;
        {
            const char *src = input_buffer + 5;
            int si = 0, ci = 0;
            rout_file[0] = '\0';
            rin_file[0]  = '\0';
            /* Ekstrak bagian sebelum '>'/'<' ke cmd_buf (berisi prog + semua arg) */
            while (src[si] && src[si] != '>' && src[si] != '<' && ci < 127)
                cmd_buf[ci++] = src[si++];
            while (ci > 0 && cmd_buf[ci-1] == ' ') ci--;  /* trim trailing space */
            cmd_buf[ci] = '\0';
            /* Tokenize cmd_buf → exec_argv[0]=prog exec_argv[1]=arg1 ... */
            {
                char *p = cmd_buf;
                while (*p == ' ') p++;
                while (*p && exec_argc < 8) {
                    exec_argv[exec_argc++] = p;
                    while (*p && *p != ' ') p++;
                    if (*p) { *p = '\0'; p++; while (*p == ' ') p++; }
                }
                exec_argv[exec_argc] = 0;
            }
            /* Nama program = token pertama */
            exec_name[0] = '\0';
            if (exec_argc > 0) {
                int ni = 0;
                const char *s0 = exec_argv[0];
                while (s0[ni] && ni < 63) exec_name[ni] = s0[ni], ni++;
                exec_name[ni] = '\0';
            }
            /* Parse redirect operators (sisa setelah bagian program+args) */
            while (src[si]) {
                char op = src[si++];        /* '>' atau '<' */
                int fi = 0;
                while (src[si] == ' ') si++;
                while (src[si] && src[si] != '>' && src[si] != '<' && src[si] != ' ' && fi < 31)
                    if (op == '>') rout_file[fi++] = src[si++];
                    else           rin_file[fi++]  = src[si++];
                if (op == '>') rout_file[fi] = '\0';
                else           rin_file[fi]  = '\0';
                /* skip sisa sampai operator berikutnya */
                while (src[si] && src[si] != '>' && src[si] != '<') si++;
            }
        }
        const char *name = exec_name;
        uint32_t size;
        const uint8_t *data = fs_read_bin(name, &size);
        if (!data) {
            print("exec: file tidak ditemukan: ");
            print(name);
            print("\n");
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
                /* Matikan interrupt agar redirect diterapkan sebelum task sempat dijadwal */
                __asm__ volatile("cli" ::: "memory");
                int tid = task_create_user(entry, proc_dir, user_esp, name,
                                           exec_argc, exec_argv);
                /* Terapkan redirect I/O sebelum task mulai berjalan */
                if (rout_file[0]) vfs_redirect_out(tid, rout_file);
                if (rin_file[0])  vfs_redirect_in(tid, rin_file);
                __asm__ volatile("sti" ::: "memory");
                if (bg_exec) {
                    char tbuf[8]; itoa((uint32_t)tid, tbuf);
                    print("exec: ["); print(tbuf); print("] "); print(name); print(" &\n");
                } else {
                    /* Foreground: blok shell sampai program selesai.
                     * F-T: daftarkan tid ke keyboard agar Ctrl+C kirim SIGINT. */
                    keyboard_set_fg_pid(tid);
                    task_wait(tid);
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
            print("curl: gunakan: curl http://hostname/path\n");
        } else {
            http_get(url);
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
             * Format: progname [arg1 arg2 ...]
             * Jika program ditemukan di filesystem, run langsung tanpa 'exec ' prefix. */
            char ae_buf[128];
            const char *ae_argv[9];
            int ae_argc = 0;
            int ae_i = 0;
            /* Salin input ke ae_buf */
            while (input_buffer[ae_i] && ae_i < 127) { ae_buf[ae_i] = input_buffer[ae_i]; ae_i++; }
            ae_buf[ae_i] = '\0';
            /* Tokenize */
            { char *p = ae_buf; while (*p == ' ') p++;
              while (*p && ae_argc < 8) {
                  ae_argv[ae_argc++] = p; while (*p && *p != ' ') p++;
                  if (*p) { *p = '\0'; p++; while (*p == ' ') p++; }
              } ae_argv[ae_argc] = 0; }
            /* Cari program di filesystem */
            if (ae_argc > 0) {
                char ae_name[64];
                int ni = 0;
                const char *s0 = ae_argv[0];
                while (s0[ni] && ni < 63) ae_name[ni] = s0[ni], ni++;
                ae_name[ni] = '\0';
                uint32_t ae_size;
                const uint8_t *ae_data = fs_read_bin(ae_name, &ae_size);
                if (ae_data) {
                    uint64_t *ae_dir = vmm_create_page_dir();
                    uint64_t ae_entry = elf_load(ae_data, ae_size, ae_dir);
                    if (ae_entry) {
                        uint64_t ae_sp = pmm_alloc_frame();
                        vmm_map_page(ae_dir, 0x600000, ae_sp, 7);
                        __asm__ volatile("cli" ::: "memory");
                        int ae_tid = task_create_user(ae_entry, ae_dir, 0x600000 + PAGE_SIZE,
                                                      ae_name, ae_argc, ae_argv);
                        __asm__ volatile("sti" ::: "memory");
                        keyboard_set_fg_pid(ae_tid);
                        task_wait(ae_tid);
                        keyboard_set_fg_pid(-1);
                    } else {
                        set_color(GFX_LRED, GFX_BLACK);
                        print("Perintah tidak dikenal: ");
                        print(input_buffer); print("\n");
                        set_color(GFX_WHITE, GFX_BLACK);
                    }
                } else {
                    set_color(GFX_LRED, GFX_BLACK);
                    print("Perintah tidak dikenal: ");
                    print(input_buffer); print("\n");
                    set_color(GFX_WHITE, GFX_BLACK);
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