/* edit.c — Fondasi AE: text editor nano-like.
 * Ctrl+S = save,  Ctrl+Q = quit,  Ctrl+G = goto line
 * Arrow keys = navigate, Backspace = delete, Enter = newline
 * Supports files up to 8192 bytes, max 256 lines.
 */
#include "lib.h"

/* ── constants ─────────────────────────────────────────── */
#define SCREEN_COLS 160
#define SCREEN_ROWS  90
#define BODY_ROWS   (SCREEN_ROWS - 2)   /* top header + bottom status */
#define MAX_BUF      8192
#define MAX_LINES    256

/* ── key codes from keyboard driver ────────────────────── */
#define K_UP      0x01
#define K_DOWN    0x02
#define K_LEFT    0x04
#define K_RIGHT   0x05
#define K_DEL     0x06
#define K_BS      '\b'
#define K_ENTER   '\n'
#define K_CTRL_S  0x13     /* Ctrl+S = 19 */
#define K_CTRL_Q  0x11     /* Ctrl+Q = 17 */
#define K_CTRL_G  0x07     /* Ctrl+G = 7  */

/* ── editor state ───────────────────────────────────────── */
static char     buf[MAX_BUF];        /* flat text buffer   */
static int      blen  = 0;           /* total chars in buf */
static int      cur   = 0;           /* cursor byte offset */
static int      top   = 0;           /* first visible line  */
static int      dirty = 0;           /* unsaved changes?    */
static char     fname[64];

/* ── helpers ─────────────────────────────────────────────── */
static void pstr(const char *s) { print(s); }

static void pchar(char c) {
    char tmp[2]; tmp[0] = c; tmp[1] = '\0';
    pstr(tmp);
}

static int slen(const char *s) { int n=0; while(s[n]) n++; return n; }

static void sitoa(int v, char *out) {
    if (v == 0) { out[0]='0'; out[1]='\0'; return; }
    char tmp[12]; int i=0;
    if (v<0){out[0]='-';out++;v=-v;}
    while(v){tmp[i++]=(char)('0'+v%10);v/=10;}
    int j=0;while(i>0)out[j++]=tmp[--i];out[j]='\0';
}

static void gotoxy(int r, int c) { ansi_gotoxy(r+1, c+1); }  /* convert 0-based to 1-based */

/* count line number (0-based) for byte offset p */
static int byte_to_line(int p) {
    int ln=0, i;
    for(i=0;i<p && i<blen;i++) if(buf[i]=='\n') ln++;
    return ln;
}

/* return byte offset of start of line ln */
static int line_start(int ln) {
    int i, l=0;
    for(i=0;i<blen;i++){
        if(l==ln) return i;
        if(buf[i]=='\n') l++;
    }
    return blen;
}

/* return column (0-based) of offset p */
static int byte_to_col(int p) {
    int c=0, i;
    for(i=p-1;i>=0;i--){ if(buf[i]=='\n') break; c++; }
    return c;
}

/* total line count */
static int total_lines(void) {
    int l=1, i;
    for(i=0;i<blen;i++) if(buf[i]=='\n') l++;
    return l;
}

/* ── draw ──────────────────────────────────────────────── */
static void draw_header(void) {
    gotoxy(0, 0);
    pstr(ANSI_BOLD "\033[44m\033[97m");   /* bold white on blue */
    int i;
    for(i=0;i<SCREEN_COLS;i++) pchar(' ');
    gotoxy(0, 2);
    pstr("EDIT: ");
    pstr(fname);
    if(dirty){ pstr(" [modified]"); }
    pstr(ANSI_RESET);
}

static void draw_status(const char *msg) {
    gotoxy(SCREEN_ROWS-1, 0);
    pstr("\033[44m\033[97m");
    int i;
    for(i=0;i<SCREEN_COLS;i++) pchar(' ');
    gotoxy(SCREEN_ROWS-1, 0);
    pstr(" ^S=Save  ^Q=Quit  ^G=GoLine  | ");
    if(msg) pstr(msg);
    int ln = byte_to_line(cur);
    int col = byte_to_col(cur);
    char lnbuf[16], colbuf[16];
    sitoa(ln+1, lnbuf); sitoa(col+1, colbuf);
    /* right-align: Line x Col x */
    gotoxy(SCREEN_ROWS-1, SCREEN_COLS-20);
    pstr("Ln:"); pstr(lnbuf); pstr(" Col:"); pstr(colbuf);
    pstr(ANSI_RESET);
}

static void draw_body(void) {
    int tl = total_lines();
    int r, i;
    for(r = 0; r < BODY_ROWS; r++) {
        int ln = top + r;
        gotoxy(r+1, 0);
        /* line number gutter (4 chars) */
        pstr("\033[90m");
        if(ln < tl) {
            char nb[8]; sitoa(ln+1, nb);
            int pad = 3 - slen(nb);
            while(pad-->0) pchar(' ');
            pstr(nb); pchar(' ');
        } else {
            pstr("    ");
        }
        pstr(ANSI_RESET);
        /* line text */
        if(ln < tl) {
            int p = line_start(ln);
            int col = 0;
            while(p < blen && buf[p] != '\n' && col < SCREEN_COLS-4) {
                pchar(buf[p]);
                p++; col++;
            }
        }
        /* clear rest of line */
        pstr("\033[0K");
    }
}

static void place_cursor(void) {
    int ln = byte_to_line(cur);
    int col = byte_to_col(cur);
    int screen_row = ln - top + 1;  /* +1 for header */
    int screen_col = col + 4;       /* +4 for gutter */
    if(screen_col >= SCREEN_COLS) screen_col = SCREEN_COLS-1;
    gotoxy(screen_row, screen_col);
}

static void full_redraw(const char *msg) {
    draw_header();
    draw_body();
    draw_status(msg);
    place_cursor();
}

/* ── file I/O ───────────────────────────────────────────── */
static void load_file(void) {
    int fd = sys_open(fname, VFS_O_RDONLY);
    if(fd < 0) { blen = 0; buf[0] = '\0'; return; }
    int n;
    blen = 0;
    while(blen < MAX_BUF-1) {
        n = sys_read_fd(fd, buf+blen, MAX_BUF-1-blen);
        if(n <= 0) break;
        blen += n;
    }
    buf[blen] = '\0';
    sys_close_fd(fd);
}

static void save_file(void) {
    int fd = sys_open(fname, VFS_O_WRONLY | VFS_O_CREATE);
    if(fd < 0) { draw_status("ERROR: cannot open for write!"); return; }
    sys_write_fd(fd, buf, blen);
    sys_close_fd(fd);
    dirty = 0;
    draw_status("Saved.");
}

/* ── editing ops ─────────────────────────────────────────── */
static void insert_char(char c) {
    if(blen >= MAX_BUF-1) return;
    /* shift right */
    int i;
    for(i = blen; i > cur; i--) buf[i] = buf[i-1];
    buf[cur] = c;
    blen++; cur++;
    buf[blen] = '\0';
    dirty = 1;
}

static void delete_before(void) {  /* Backspace */
    if(cur == 0) return;
    int i;
    cur--;
    for(i = cur; i < blen-1; i++) buf[i] = buf[i+1];
    blen--;
    buf[blen] = '\0';
    dirty = 1;
}

static void delete_at(void) {  /* Delete key */
    if(cur >= blen) return;
    int i;
    for(i = cur; i < blen-1; i++) buf[i] = buf[i+1];
    blen--;
    buf[blen] = '\0';
    dirty = 1;
}

/* ── cursor movement ──────────────────────────────────────── */
static void move_up(void) {
    int ln = byte_to_line(cur);
    if(ln == 0) return;
    int col = byte_to_col(cur);
    int prev_start = line_start(ln-1);
    /* find end of previous line */
    int prev_end = prev_start;
    while(prev_end < blen && buf[prev_end] != '\n') prev_end++;
    int prev_len = prev_end - prev_start;
    cur = prev_start + (col <= prev_len ? col : prev_len);
    if(ln-1 < top) top--;
}

static void move_down(void) {
    int ln = byte_to_line(cur);
    int tl = total_lines();
    if(ln >= tl-1) return;
    int col = byte_to_col(cur);
    int next_start = line_start(ln+1);
    /* end of next line */
    int next_end = next_start;
    while(next_end < blen && buf[next_end] != '\n') next_end++;
    int next_len = next_end - next_start;
    cur = next_start + (col <= next_len ? col : next_len);
    if(ln+1 >= top + BODY_ROWS) top++;
}

static void move_left(void) {
    if(cur > 0) cur--;
}

static void move_right(void) {
    if(cur < blen) cur++;
}

/* ── goto line ───────────────────────────────────────────── */
static void cmd_goto(void) {
    /* show prompt in status */
    gotoxy(SCREEN_ROWS-1, 0);
    pstr("\033[44m\033[97m");
    int i; for(i=0;i<SCREEN_COLS;i++) pchar(' ');
    gotoxy(SCREEN_ROWS-1, 0);
    pstr(" Goto line: ");
    pstr(ANSI_RESET);
    /* read number */
    char nbuf[8]; int ni = 0;
    while(1) {
        char c = (char)sys_read_fd(0, nbuf+ni, 1);
        if(c <= 0) continue;
        if(c == K_ENTER) break;
        if(c == K_BS && ni > 0) { ni--; pstr("\b \b"); continue; }
        if(c >= '0' && c <= '9' && ni < 6) { nbuf[ni++] = c; pchar(c); }
    }
    nbuf[ni] = '\0';
    if(ni == 0) return;
    int target = 0;
    for(i=0;i<ni;i++) target = target*10 + (nbuf[i]-'0');
    target--;  /* 1-based → 0-based */
    int tl = total_lines();
    if(target < 0) target = 0;
    if(target >= tl) target = tl-1;
    cur = line_start(target);
    /* adjust scroll */
    if(target < top) top = target;
    if(target >= top + BODY_ROWS) top = target - BODY_ROWS/2;
    if(top < 0) top = 0;
}

/* ── main ────────────────────────────────────────────────── */
void _start(void) {
    static char argbuf[512];
    static char *argv[9];
    int argc = getargv(argbuf, argv);

    if(argc < 2 || !argv[1] || !argv[1][0]) {
        pstr("Usage: edit <filename>\n");
        exit_code(1);
    }

    /* copy filename */
    int i;
    for(i=0; i<63 && argv[1][i]; i++) fname[i] = argv[1][i];
    fname[i] = '\0';

    load_file();
    cur = 0; top = 0; dirty = 0;

    pstr(ANSI_CLEAR ANSI_HOME);
    full_redraw(0);

    while(1) {
        /* read one char from stdin */
        char c;
        int n = sys_read_fd(0, &c, 1);
        if(n <= 0) continue;

        if(c == K_CTRL_Q) {
            if(dirty) {
                draw_status("Unsaved changes! Press ^Q again to quit.");
                char c2;
                while((n = sys_read_fd(0, &c2, 1)) <= 0);
                if(c2 != K_CTRL_Q) { full_redraw(0); continue; }
            }
            break;
        } else if(c == K_CTRL_S) {
            save_file();
            full_redraw("Saved.");
            continue;
        } else if(c == K_CTRL_G) {
            cmd_goto();
            full_redraw(0);
            continue;
        } else if(c == K_UP)    { move_up();    }
        else if(c == K_DOWN)    { move_down();  }
        else if(c == K_LEFT)    { move_left();  }
        else if(c == K_RIGHT)   { move_right(); }
        else if(c == K_BS)      { delete_before(); }
        else if(c == K_DEL)     { delete_at();     }
        else if(c == K_ENTER)   { insert_char('\n'); }
        else if(c >= 0x20)      { insert_char(c);    }
        else { full_redraw(0); continue; }

        full_redraw(0);
    }

    /* restore terminal */
    pstr(ANSI_CLEAR ANSI_HOME ANSI_RESET);
    exit_code(0);
}
