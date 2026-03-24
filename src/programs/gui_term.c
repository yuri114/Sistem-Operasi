/*
 * gui_term.c — Terminal GUI
 *
 * Fitur:
 *  - Prompt $ dengan input baris aktif dan kursor
 *  - Riwayat perintah (↑/↓ untuk navigasi)
 *  - Tab completion dari filesystem
 *  - Perintah: exec program, clear/cls, help
 *  - Scroll output (18 baris terlihat)
 */
#include "lib.h"

/* ---- Dimensi ---- */
#define WIN_X           80
#define WIN_Y           60
#define WIN_W          400
#define WIN_H          260
#define TEXT_X           4
#define TEXT_Y           4
#define LINE_H          10
#define OUTPUT_LINES    18   /* baris output yang terlihat */
#define MAX_COLS        44   /* karakter per baris (44×8=352px < CA_W=398) */
#define HIST_SIZE       16   /* entri riwayat perintah */

#define PROMPT_Y  (TEXT_Y + OUTPUT_LINES * LINE_H)   /* 184 */
#define BTN_Y     (PROMPT_Y + LINE_H + 4)            /* 198 */
#define BTN_H      18

/* ---- Status tombol ---- */
#define BTN_CLEAR  0
#define BTN_CLOSE  1

/* ---- State ---- */
static char out[OUTPUT_LINES][MAX_COLS + 2];  /* output scrollback */
static char input_buf[MAX_COLS + 1];           /* baris input saat ini */
static int  input_len;                         /* panjang input */
static char hist[HIST_SIZE][MAX_COLS + 1];     /* riwayat perintah */
static int  hist_count;                        /* total riwayat tersimpan */
static int  hist_pos;                          /* posisi browse (-1 = input aktif) */
static char hist_save[MAX_COLS + 1];           /* simpan input saat mulai browse */
static int  win_id;

/* ---- Utilitas string lokal ---- */
static int  gt_len(const char *s) { int n=0; while(s[n]) n++; return n; }
static void gt_cpy(char *d, const char *s, int max) {
    int i=0; while(i<max-1 && s[i]) { d[i]=s[i]; i++; } d[i]='\0';
}
static int  gt_startswith(const char *s, const char *pre) {
    int i=0;
    while(pre[i] && s[i]==pre[i]) i++;
    return pre[i] == '\0';
}

/* ---- Output scroll ---- */
static void out_scroll(void) {
    int i;
    for (i = 0; i < OUTPUT_LINES - 1; i++)
        gt_cpy(out[i], out[i+1], MAX_COLS+2);
    out[OUTPUT_LINES-1][0] = '\0';
}

static void out_print(const char *msg) {
    /* Jika baris terakhir sudah terisi, scroll dulu */
    if (out[OUTPUT_LINES-1][0] != '\0') out_scroll();
    gt_cpy(out[OUTPUT_LINES-1], msg, MAX_COLS+2);
}

/* ---- Render ---- */
static void do_render(void) {
    int i;
    win_clear(win_id, GFX_BLACK);

    /* Output lines */
    for (i = 0; i < OUTPUT_LINES; i++)
        win_draw(win_id, TEXT_X, TEXT_Y + i*LINE_H, out[i], GFX_LGREEN, GFX_BLACK);

    /* Prompt baris input: "$ " + input + kursor */
    char prompt[MAX_COLS + 6];
    prompt[0]='$'; prompt[1]=' ';
    int pi = 2;
    int il = input_len < MAX_COLS ? input_len : MAX_COLS;
    int k;
    for (k = 0; k < il; k++) prompt[pi++] = input_buf[k];
    prompt[pi++] = '_';   /* kursor */
    prompt[pi]   = '\0';
    win_draw(win_id, TEXT_X, PROMPT_Y, prompt, GFX_WHITE, GFX_BLACK);
}

/* ---- Simpan ke riwayat ---- */
static void hist_push(const char *cmd) {
    if (cmd[0] == '\0') return;
    /* Hindari duplikat berurutan */
    if (hist_count > 0 && gt_len(hist[(hist_count-1) % HIST_SIZE]) == gt_len(cmd)) {
        int same = 1, i;
        for (i = 0; hist[(hist_count-1)%HIST_SIZE][i] && cmd[i]; i++)
            if (hist[(hist_count-1)%HIST_SIZE][i] != cmd[i]) { same=0; break; }
        if (same && hist[(hist_count-1)%HIST_SIZE][i]==cmd[i]) return;
    }
    gt_cpy(hist[hist_count % HIST_SIZE], cmd, MAX_COLS+1);
    hist_count++;
    if (hist_count > HIST_SIZE * 1024) hist_count = HIST_SIZE; /* overflow guard */
}

/* ---- Tab completion dari fs_list ---- */
static void do_tab(void) {
    static char list_buf[512];
    int count = fs_list(list_buf, 512);
    if (count <= 0) return;

    /* Kumpulkan nama yang cocok */
    static char matches[16][32];
    int mc = 0;
    int pos = 0;
    while (list_buf[pos] && mc < 16) {
        char name[32]; int ni = 0;
        while (list_buf[pos] && list_buf[pos]!='\n' && ni<31)
            name[ni++] = list_buf[pos++];
        name[ni] = '\0';
        if (list_buf[pos]=='\n') pos++;
        if (ni > 0 && gt_startswith(name, input_buf))
            gt_cpy(matches[mc++], name, 32);
    }

    if (mc == 0) return;  /* tidak ada kecocokan */

    if (mc == 1) {
        /* Lengkapi input dengan satu-satunya pilihan */
        gt_cpy(input_buf, matches[0], MAX_COLS+1);
        input_len = gt_len(input_buf);
    } else {
        /* Tampilkan semua pilihan di output */
        char row[MAX_COLS+2]; int ri = 0, i;
        for (i = 0; i < mc; i++) {
            int nl = gt_len(matches[i]);
            if (ri + nl + 1 >= MAX_COLS) {
                row[ri] = '\0';
                out_print(row);
                ri = 0;
            }
            int j;
            for (j = 0; j < nl; j++) row[ri++] = matches[i][j];
            row[ri++] = ' ';
        }
        if (ri > 0) { row[ri]='\0'; out_print(row); }
    }
    do_render();
}

/* ---- Eksekusi perintah ---- */
static void do_exec(void) {
    if (input_len == 0) return;
    input_buf[input_len] = '\0';

    /* Tampilkan perintah di output */
    char echo[MAX_COLS+4];
    echo[0]='$'; echo[1]=' ';
    int ei=2, i;
    for (i=0; i<input_len && ei<MAX_COLS+3; i++) echo[ei++]=input_buf[i];
    echo[ei]='\0';
    out_print(echo);

    hist_push(input_buf);
    hist_pos = -1;

    /* Built-in: clear / cls */
    if ((input_len==5 && input_buf[0]=='c' && input_buf[1]=='l' &&
         input_buf[2]=='e' && input_buf[3]=='a' && input_buf[4]=='r') ||
        (input_len==3 && input_buf[0]=='c' && input_buf[1]=='l' && input_buf[2]=='s')) {
        int j;
        for (j=0; j<OUTPUT_LINES; j++) out[j][0]='\0';

    /* Built-in: help */
    } else if (input_len==4 && input_buf[0]=='h' && input_buf[1]=='e' &&
               input_buf[2]=='l' && input_buf[3]=='p') {
        out_print("Perintah tersedia:");
        out_print(" <nama_program>  jalankan program");
        out_print(" clear/cls       bersihkan layar");
        out_print(" Tab             lengkapi nama");
        out_print(" Up/Down         riwayat perintah");

    /* Jalankan sebagai program */
    } else {
        int r = exec(input_buf);
        if (r < 0) {
            char err[MAX_COLS+2];
            err[0]='E'; err[1]='r'; err[2]='r'; err[3]=':'; err[4]=' ';
            int ei2=5;
            for (i=0; i<input_len && ei2<MAX_COLS+1; i++) err[ei2++]=input_buf[i];
            err[ei2++]=' '; err[ei2++]='t'; err[ei2++]='i'; err[ei2++]='d'; err[ei2++]='a'; err[ei2++]='k'; err[ei2]='\0';
            out_print(err);
        }
    }

    /* Reset input */
    input_buf[0] = '\0';
    input_len = 0;
    do_render();
}

/* ---- Entry point ---- */
void _start(void) {
    int i;
    win_id = win_create(WIN_X, WIN_Y, WIN_W, WIN_H, "Terminal");
    if (win_id < 0) exit();

    /* Init */
    for (i = 0; i < OUTPUT_LINES; i++) out[i][0] = '\0';
    input_buf[0] = '\0';
    input_len     = 0;
    hist_count    = 0;
    hist_pos      = -1;

    /* Tombol */
    win_btn_add(win_id, TEXT_X,      BTN_Y, 52, BTN_H, "Clear");
    win_btn_add(win_id, TEXT_X + 56, BTN_Y, 52, BTN_H, "Close");

    out_print("Terminal siap. Ketik 'help' untuk bantuan.");
    do_render();

    /* Event loop */
    for (;;) {
        int ev = win_poll(win_id);
        if (ev == WIN_EVENT_NONE) { yield(); continue; }

        int t = WIN_EV_TYPE(ev);

        if (t == WIN_EVENT_CLOSE) break;

        if (t == WIN_EVENT_BTN) {
            int b = WIN_EV_BTN(ev);
            if (b == BTN_CLEAR) {
                for (i=0; i<OUTPUT_LINES; i++) out[i][0]='\0';
                do_render();
            }
            if (b == BTN_CLOSE) break;
        }

        if (t == WIN_EVENT_KEY) {
            char c = WIN_EV_CHAR(ev);

            if (c == '\r' || c == '\n') {
                do_exec();

            } else if (c == '\b') {
                if (input_len > 0) { input_len--; input_buf[input_len]='\0'; }
                do_render();

            } else if (c == '\x03') { /* Tab */
                do_tab();

            } else if (c == '\x01') { /* ↑ — riwayat lebih lama */
                int avail = hist_count < HIST_SIZE ? hist_count : HIST_SIZE;
                if (avail == 0) continue;
                if (hist_pos == -1) {
                    gt_cpy(hist_save, input_buf, MAX_COLS+1);
                    hist_pos = hist_count - 1;
                } else if (hist_pos > hist_count - avail) {
                    hist_pos--;
                }
                gt_cpy(input_buf, hist[hist_pos % HIST_SIZE], MAX_COLS+1);
                input_len = gt_len(input_buf);
                do_render();

            } else if (c == '\x02') { /* ↓ — riwayat lebih baru */
                if (hist_pos == -1) continue;
                if (hist_pos == hist_count - 1) {
                    hist_pos = -1;
                    gt_cpy(input_buf, hist_save, MAX_COLS+1);
                } else {
                    hist_pos++;
                    gt_cpy(input_buf, hist[hist_pos % HIST_SIZE], MAX_COLS+1);
                }
                input_len = gt_len(input_buf);
                do_render();

            } else if (c >= 32 && c < 127) {
                if (input_len < MAX_COLS) {
                    input_buf[input_len++] = c;
                    input_buf[input_len]   = '\0';
                }
                hist_pos = -1;
                do_render();
            }
        }
    }

    win_destroy(win_id);
    exit();
}

