/*
 * notepad.c — Editor teks sederhana berbasis window GUI
 *
 * Fitur:
 *  - Edit teks multi-baris (60 karakter/baris, 200 baris max)
 *  - Tombol New, Save (simpan ke "note.txt"), Close
 *  - Kursor teks navigasi via klik dan keyboard
 *  - Simpan / muat dari filesystem kernel
 *
 * Constraint: WIN_TEXTBUF=32 → 1 win_clear + 28 baris + 1 status = 30 entries ✓
 */
#include "lib.h"

/* ---- Dimensi window & layout ---- */
#define WIN_W           522
#define WIN_H           360
#define CHARS_PER_LINE   60   /* maks karakter per baris (60×8=480px < CA_W=520) */
#define MAX_LINES       200
#define TEXT_Y           24   /* y awal area teks di koordinat konten */
#define LINE_H           10   /* tinggi per baris (font 8px + 2px spasi) */
#define VISIBLE_LINES    28   /* jumlah baris yang terlihat sekaligus */
#define STATUS_Y        306   /* TEXT_Y + VISIBLE_LINES*LINE_H + 2 */
#define SCROLL_X        512   /* x scrollbar (CA_W-8 = 520-8) */
#define SCROLL_W          8   /* lebar strip scrollbar */
#define SCROLL_TRACK_H  280   /* VISIBLE_LINES * LINE_H */

/* ---- Indeks tombol ---- */
#define BTN_NEW    0
#define BTN_SAVE   1
#define BTN_CLOSE  2

/* ---- Buffer teks ---- */
static char lines[MAX_LINES][CHARS_PER_LINE + 1];
static int  line_count;   /* total baris (minimal 1) */
static int  cur_line;     /* baris kursor (0-based) */
static int  cur_col;      /* kolom kursor (0-based, 0..strlen(lines[cur_line])) */
static int  top_line;     /* baris pertama yang terlihat */

static char filename[32]; /* nama file aktif */

/* ---- Utilitas string lokal (hindari konflik dengan lib.h) ---- */
static int  np_len(const char *s)       { int n=0; while(s[n]) n++; return n; }

static void np_ncpy(char *d, const char *s, int n) {
    int i; for (i=0; i<n-1 && s[i]; i++) d[i]=s[i]; d[i]='\0';
}

static void np_cat(char *d, const char *s) {
    int i=np_len(d), j=0; while(s[j]) d[i++]=s[j++]; d[i]='\0';
}

/* ---- Operasi teks ---- */
static void clear_text(void) {
    int i;
    for (i=0; i<MAX_LINES; i++) lines[i][0] = '\0';
    line_count = 1;
    cur_line   = 0;
    cur_col    = 0;
    top_line   = 0;
}

static void insert_char(char c) {
    char *ln = lines[cur_line];
    int   len = np_len(ln);
    if (len >= CHARS_PER_LINE) return;   /* baris penuh */
    int k;
    for (k = len; k > cur_col; k--) ln[k] = ln[k-1];
    ln[cur_col] = c;
    ln[len+1]  = '\0';
    cur_col++;
}

static void do_backspace(void) {
    if (cur_col > 0) {
        char *ln  = lines[cur_line];
        int   len = np_len(ln);
        int   k;
        for (k = cur_col-1; k < len-1; k++) ln[k] = ln[k+1];
        ln[len-1] = '\0';
        cur_col--;
    } else if (cur_line > 0) {
        /* gabung baris ini ke baris sebelumnya */
        char *prev     = lines[cur_line-1];
        char *curr     = lines[cur_line];
        int   prev_len = np_len(prev);
        int   curr_len = np_len(curr);
        if (prev_len + curr_len <= CHARS_PER_LINE) {
            int k;
            for (k=0; k<curr_len; k++) prev[prev_len+k] = curr[k];
            prev[prev_len+curr_len] = '\0';
            /* geser baris ke atas */
            for (k = cur_line; k < line_count-1; k++) {
                int j;
                for (j=0; j<=CHARS_PER_LINE; j++) lines[k][j] = lines[k+1][j];
            }
            lines[line_count-1][0] = '\0';
            line_count--;
            cur_line--;
            cur_col = prev_len;
            if (top_line > cur_line) top_line = cur_line;
        }
    }
}

static void do_enter(void) {
    if (line_count >= MAX_LINES) return;
    char *curr     = lines[cur_line];
    int   curr_len = np_len(curr);
    int   k;
    /* geser baris ke bawah */
    for (k = line_count; k > cur_line+1; k--) {
        int j;
        for (j=0; j<=CHARS_PER_LINE; j++) lines[k][j] = lines[k-1][j];
    }
    /* salin sisa baris ke baris baru */
    char *next = lines[cur_line+1];
    int ni = 0;
    for (k = cur_col; k < curr_len; k++) next[ni++] = curr[k];
    next[ni] = '\0';
    /* potong baris sekarang */
    curr[cur_col] = '\0';
    line_count++;
    cur_line++;
    cur_col = 0;
    if (cur_line >= top_line + VISIBLE_LINES) top_line++;
}

/* ---- Scrollbar ---- */
static void draw_scrollbar(int id) {
    /* Track */
    win_fill_rect(id, SCROLL_X, TEXT_Y, SCROLL_W, SCROLL_TRACK_H, GFX_DGRAY);
    /* Border kiri track */
    win_fill_rect(id, SCROLL_X, TEXT_Y, 1, SCROLL_TRACK_H, GFX_BLACK);
    if (line_count <= VISIBLE_LINES) {
        /* Semua baris muat: thumb penuh */
        win_fill_rect(id, SCROLL_X+1, TEXT_Y, SCROLL_W-1, SCROLL_TRACK_H, GFX_LGRAY);
    } else {
        int thumb_h = SCROLL_TRACK_H * VISIBLE_LINES / line_count;
        if (thumb_h < 6) thumb_h = 6;
        int max_scroll = line_count - VISIBLE_LINES;
        int thumb_y = TEXT_Y + (SCROLL_TRACK_H - thumb_h) * top_line / max_scroll;
        win_fill_rect(id, SCROLL_X+1, thumb_y, SCROLL_W-1, thumb_h, GFX_LGRAY);
    }
}

/* ---- Render ---- */
static void do_render(int id) {
    int i;
    win_clear(id, GFX_BLACK);

    /* Gambar baris teks yang terlihat */
    for (i = 0; i < VISIBLE_LINES; i++) {
        int li = top_line + i;
        int ry = TEXT_Y + i * LINE_H;

        if (li < line_count) {
            /* Bangun string tampilan dengan kursor | */
            char disp[CHARS_PER_LINE + 3];
            int  di = 0;
            int  ln_len = np_len(lines[li]);
            int  k;
            for (k = 0; k < ln_len && di < CHARS_PER_LINE + 1; k++) {
                if (li == cur_line && k == cur_col && di < CHARS_PER_LINE + 1)
                    disp[di++] = '|';
                if (di < CHARS_PER_LINE + 1)
                    disp[di++] = lines[li][k];
            }
            if (li == cur_line && cur_col == ln_len && di < CHARS_PER_LINE + 1)
                disp[di++] = '|';
            disp[di] = '\0';
            win_draw(id, 0, ry, disp, GFX_WHITE, GFX_BLACK);
        } else {
            win_draw(id, 0, ry, "", GFX_WHITE, GFX_BLACK);
        }
    }

    /* Status bar: "Ln:NNN Col:NN  filename.txt" dipad ke 62 karakter */
    char st[64];
    int  ln1  = cur_line + 1;
    int  col1 = cur_col  + 1;
    st[0]='L'; st[1]='n'; st[2]=':';
    st[3]='0'+ln1/100; st[4]='0'+(ln1/10)%10; st[5]='0'+ln1%10;
    st[6]=' '; st[7]='C'; st[8]='o'; st[9]='l'; st[10]=':';
    st[11]='0'+col1/10; st[12]='0'+col1%10;
    st[13]=' '; st[14]='\0';
    np_cat(st, filename);
    /* pad ke 62 karakter agar memenuhi lebar CA */
    int slen = np_len(st);
    while (slen < 62) st[slen++] = ' ';
    st[62] = '\0';
    win_draw(id, 0, STATUS_Y, st, GFX_WHITE, GFX_DGRAY);
    draw_scrollbar(id);
}

/* ---- Load / Save ---- */
static void do_load(void) {
    static char load_buf[16384];
    int loaded = fs_read(filename, load_buf, sizeof(load_buf));
    if (loaded <= 0) return;
    const char *data = load_buf;
    clear_text();
    int ci = 0, li = 0, col = 0;
    while (data[ci] && li < MAX_LINES) {
        char c = data[ci++];
        if (c == '\n') {
            li++;
            if (li >= line_count) line_count = li + 1;
            col = 0;
        } else if (col < CHARS_PER_LINE) {
            lines[li][col]   = c;
            lines[li][col+1] = '\0';
            col++;
            if (li >= line_count) line_count = li + 1;
        }
    }
    if (line_count < 1) line_count = 1;
}

static void do_save(void) {
    /* Serialisasi baris ke buffer datar */
    static char save_buf[4096];
    int pos = 0, i;
    for (i = 0; i < line_count && pos < 4090; i++) {
        int k;
        for (k = 0; lines[i][k] && pos < 4090; k++) save_buf[pos++] = lines[i][k];
        if (i < line_count - 1 && pos < 4090) save_buf[pos++] = '\n';
    }
    save_buf[pos] = '\0';
    fs_write(filename, save_buf);
}

/* ---- Entry point ---- */
void _start(void) {
    clear_text();
    np_ncpy(filename, "note.txt", 32);

    int id = win_create((SCREEN_W - WIN_W) / 2,
                        (SCREEN_H - 16 - WIN_H) / 2,
                        WIN_W, WIN_H, "Notepad");
    if (id < 0) exit();

    /* Toolbar: New | Save | (gap) | Close */
    win_btn_add(id,  4,           2, 38, 18, "New");
    win_btn_add(id,  46,          2, 42, 18, "Save");
    win_btn_add(id,  WIN_W-2-56,  2, 56, 18, "Close");

    do_load();
    do_render(id);

    for (;;) {
        int ev = win_poll(id);
        if (ev == WIN_EVENT_NONE) { yield(); continue; }

        int t = WIN_EV_TYPE(ev);

        if (t == WIN_EVENT_CLOSE) break;

        if (t == WIN_EVENT_BTN) {
            int b = WIN_EV_BTN(ev);
            if (b == BTN_NEW)   { clear_text(); do_render(id); }
            if (b == BTN_SAVE)  { do_save(); }
            if (b == BTN_CLOSE) break;
        }

        if (t == WIN_EVENT_KEY) {
            char c = WIN_EV_CHAR(ev);
            if (c == '\b') {
                do_backspace();
            } else if (c == '\r' || c == '\n') {
                do_enter();
            } else if (c == '\x01') {       /* ↑ up */
                if (cur_line > 0) {
                    cur_line--;
                    int len = np_len(lines[cur_line]);
                    if (cur_col > len) cur_col = len;
                    if (cur_line < top_line) top_line = cur_line;
                }
            } else if (c == '\x02') {       /* ↓ down */
                if (cur_line < line_count - 1) {
                    cur_line++;
                    int len = np_len(lines[cur_line]);
                    if (cur_col > len) cur_col = len;
                    if (cur_line >= top_line + VISIBLE_LINES)
                        top_line = cur_line - VISIBLE_LINES + 1;
                }
            } else if (c == '\x04') {       /* ← left */
                if (cur_col > 0) {
                    cur_col--;
                } else if (cur_line > 0) {
                    cur_line--;
                    cur_col = np_len(lines[cur_line]);
                    if (cur_line < top_line) top_line = cur_line;
                }
            } else if (c == '\x05') {       /* → right */
                int len = np_len(lines[cur_line]);
                if (cur_col < len) {
                    cur_col++;
                } else if (cur_line < line_count - 1) {
                    cur_line++;
                    cur_col = 0;
                    if (cur_line >= top_line + VISIBLE_LINES)
                        top_line = cur_line - VISIBLE_LINES + 1;
                }
            } else if (c >= 32 && c < 127) {
                insert_char(c);
            } else if (c == 17) {       /* Ctrl+Q: keluar */
                break;
            } else if (c == 19) {       /* Ctrl+S: simpan */
                do_save();
            }
            do_render(id);
        }

        if (t == WIN_EVENT_CLICK) {
            int cx, cy;
            win_click_pos(id, &cx, &cy);
            /* Klik di area scrollbar */
            if (cx >= SCROLL_X && cy >= TEXT_Y && cy < TEXT_Y + SCROLL_TRACK_H) {
                if (line_count > VISIBLE_LINES) {
                    int rel = cy - TEXT_Y;
                    top_line = rel * (line_count - VISIBLE_LINES) / (SCROLL_TRACK_H - 1);
                    if (top_line > line_count - VISIBLE_LINES)
                        top_line = line_count - VISIBLE_LINES;
                    if (cur_line < top_line) cur_line = top_line;
                    if (cur_line >= top_line + VISIBLE_LINES)
                        cur_line = top_line + VISIBLE_LINES - 1;
                }
                do_render(id);
            } else if (cy >= TEXT_Y && cy < STATUS_Y) {
                int clicked_line = top_line + (cy - TEXT_Y) / LINE_H;
                if (clicked_line >= 0 && clicked_line < line_count) {
                    cur_line = clicked_line;
                    int c2 = cx / 8;
                    int len2 = np_len(lines[cur_line]);
                    if (c2 < 0)    c2 = 0;
                    if (c2 > len2) c2 = len2;
                    cur_col = c2;
                    do_render(id);
                }
            }
        }
    }

    win_destroy(id);
    exit();
}
