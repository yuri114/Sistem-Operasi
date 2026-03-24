/*
 * gui_term.c — Terminal GUI sederhana
 *
 * Fitur:
 *  - Window "Terminal" 400x260 piksel
 *  - Tampilan teks keyboard input per baris
 *  - Tombol "Clear" (hapus semua teks) dan "Close" (tutup window)
 *  - Backspace didukung
 *
 * Cara pakai: dari shell ketik "gui_term" (jika sudah didaftarkan)
 */
#include "lib.h"

/* Dimensi window */
#define WIN_X  80
#define WIN_Y  60
#define WIN_W  400
#define WIN_H  260

/* Layout area konten (pixel di dalam window) */
#define TEXT_X    4
#define TEXT_Y    4
#define LINE_H    10   /* spasi antar baris */
#define MAX_LINES 16   /* baris teks yang tampil */
#define MAX_COLS  46   /* karakter per baris (400 - 2*border - 8px padding = ~48 kolom) */

/* Tombol di bawah area teks */
#define BTN_Y   (TEXT_Y + MAX_LINES * LINE_H + 6)
#define BTN_H   16
#define BTN_CLEAR_X  4
#define BTN_CLEAR_W  52
#define BTN_CLOSE_X  62
#define BTN_CLOSE_W  52

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
static char lines[MAX_LINES][MAX_COLS + 1];   /* teks tiap baris */
static int  cur_line = 0;                      /* baris aktif */
static int  cur_col  = 0;                      /* kolom aktif */
static int  win_id   = -1;

/* ------------------------------------------------------------------ */
/* Helper internal                                                     */
/* ------------------------------------------------------------------ */
static void redraw_line(int line) {
    if (line < 0 || line >= MAX_LINES) return;
    win_draw(win_id,
             TEXT_X, TEXT_Y + line * LINE_H,
             lines[line],
             GFX_WHITE, GFX_BLACK);
}

static void clear_all(void) {
    for (int i = 0; i < MAX_LINES; i++) {
        int k;
        for (k = 0; k < MAX_COLS; k++) lines[i][k] = ' ';
        lines[i][MAX_COLS] = '\0';
    }
    cur_line = 0;
    cur_col  = 0;
    win_clear(win_id, GFX_BLACK);
}

/* Turun satu baris (scroll jika sudah di baris terakhir) */
static void newline(void) {
    cur_col = 0;
    cur_line++;
    if (cur_line >= MAX_LINES) {
        /* scroll: geser semua baris ke atas */
        for (int i = 0; i < MAX_LINES - 1; i++) {
            int k;
            for (k = 0; k <= MAX_COLS; k++)
                lines[i][k] = lines[i + 1][k];
        }
        cur_line = MAX_LINES - 1;
        /* kosongkan baris terakhir */
        for (int k = 0; k < MAX_COLS; k++) lines[cur_line][k] = ' ';
        lines[cur_line][MAX_COLS] = '\0';

        /* Redraw semua baris */
        for (int i = 0; i < MAX_LINES; i++)
            redraw_line(i);
    }
}

/* Proses satu karakter keyboard */
static void process_char(char c) {
    if (c == '\r' || c == '\n') {
        newline();
        return;
    }
    if (c == '\b') {
        /* backspace */
        if (cur_col > 0) {
            cur_col--;
            lines[cur_line][cur_col] = ' ';
            redraw_line(cur_line);
        }
        return;
    }
    /* Karakter biasa */
    if (cur_col >= MAX_COLS) {
        newline();
    }
    lines[cur_line][cur_col] = c;
    cur_col++;
    redraw_line(cur_line);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */
void _start(void) {
    /* Buat window */
    win_id = win_create(WIN_X, WIN_Y, WIN_W, WIN_H, "Terminal");
    if (win_id < 0) exit();

    /* Init buffer teks dengan spasi */
    for (int i = 0; i < MAX_LINES; i++) {
        for (int k = 0; k < MAX_COLS; k++) lines[i][k] = ' ';
        lines[i][MAX_COLS] = '\0';
    }

    /* Tambah tombol */
    win_btn_add(win_id, BTN_CLEAR_X, BTN_Y, BTN_CLEAR_W, BTN_H, "Clear");
    win_btn_add(win_id, BTN_CLOSE_X, BTN_Y, BTN_CLOSE_W, BTN_H, "Close");

    /* Tampilkan pesan selamat datang */
    process_char('T'); process_char('e'); process_char('r'); process_char('m');
    process_char('i'); process_char('n'); process_char('a'); process_char('l');
    newline();
    process_char('K'); process_char('e'); process_char('t'); process_char('i');
    process_char('k'); process_char(' '); process_char('s'); process_char('e');
    process_char('s'); process_char('u'); process_char('a'); process_char('t');
    process_char('u'); process_char('.');
    newline();
    newline();

    /* Event loop */
    while (1) {
        int ev = win_poll(win_id);
        if (ev == WIN_EVENT_NONE) {
            yield();
            continue;
        }

        int type = WIN_EV_TYPE(ev);

        if (type == WIN_EVENT_CLOSE) {
            break;
        } else if (type == WIN_EVENT_KEY) {
            char c = WIN_EV_CHAR(ev);
            process_char(c);
        } else if (type == WIN_EVENT_BTN) {
            int btn = WIN_EV_BTN(ev);
            if (btn == 0) {
                /* Clear */
                clear_all();
            } else if (btn == 1) {
                /* Close */
                break;
            }
        }
    }

    win_destroy(win_id);
    exit();
}
