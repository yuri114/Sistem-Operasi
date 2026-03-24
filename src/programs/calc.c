/*
 * calc.c — Kalkulator GUI 4 operasi dasar (+, -, *, /)
 *
 * Layout tombol (14 tombol):
 *   [7] [8] [9] [/]
 *   [4] [5] [6] [*]
 *   [1] [2] [3] [-]
 *   [0] [C] [=] [+]
 *
 * Display: baris atas = input/hasil, baris bawah = operasi berjalan
 * Juga bisa input lewat keyboard (0-9, +-x/, Enter, Backspace, Escape)
 */
#include "lib.h"

/* Dimensi window */
#define WIN_X  180
#define WIN_Y  80
#define WIN_W  220
#define WIN_H  280

/* Layout tombol */
#define COLS    4
#define ROWS    4
#define BTN_W   46
#define BTN_H   40
#define BTN_PAD  4
#define BTN_START_X  4
#define BTN_START_Y  58   /* di bawah display */

/* Display */
#define DISP_X    4
#define DISP_Y    4
#define DISP_W   (WIN_W - 10)

/* Label 4x4 tombol kalkulator */
static const char *btn_labels[ROWS][COLS] = {
    { "7", "8", "9", "/" },
    { "4", "5", "6", "*" },
    { "1", "2", "3", "-" },
    { "0", "C", "=", "+" },
};

/* ------------------------------------------------------------------ */
/* State kalkulator                                                    */
/* ------------------------------------------------------------------ */
static int   win_id    = -1;
static long  acc       = 0;    /* akumulator (hasil operasi sebelumnya) */
static long  cur       = 0;    /* angka yang sedang diketik */
static int   has_cur   = 0;    /* apakah cur sedang aktif */
static char  op        = 0;    /* operator pending: +, -, *, / */
static int   just_eq   = 0;    /* flag: baru saja tekan = */

/* ------------------------------------------------------------------ */
/* Helper: integer to string (tanpa stdlib)                            */
/* ------------------------------------------------------------------ */
static void long_to_str(long n, char *buf) {
    if (n == 0) { buf[0]='0'; buf[1]='\0'; return; }
    char tmp[24]; int i = 0; int neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    while (n > 0) { tmp[i++] = '0' + (int)(n % 10); n /= 10; }
    if (neg) tmp[i++] = '-';
    int j;
    for (j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

/* Isi string dengan spasi sampai lebar w karakter, null-terminate */
static void pad_right(char *buf, int w) {
    int len = 0; while (buf[len]) len++;
    while (len < w) buf[len++] = ' ';
    buf[len] = '\0';
}

/* ------------------------------------------------------------------ */
/* Gambar display                                                      */
/* ------------------------------------------------------------------ */
static void draw_display(void) {
    char main_buf[24];
    char sub_buf[24];

    /* Baris utama: angka aktif atau hasil */
    if (has_cur)
        long_to_str(cur, main_buf);
    else
        long_to_str(acc, main_buf);
    pad_right(main_buf, 22);

    /* Baris bawah: operator + akumulator saat menunggu operand ke-2 */
    if (op && !just_eq) {
        long_to_str(acc, sub_buf);
        int len = 0; while (sub_buf[len]) len++;
        sub_buf[len] = ' '; sub_buf[len+1] = op; sub_buf[len+2] = '\0';
    } else {
        sub_buf[0] = ' '; sub_buf[1] = '\0';
    }
    pad_right(sub_buf, 22);

    /* Gambar di backing store */
    win_draw(win_id, DISP_X,      DISP_Y,      main_buf, 15, 0);
    win_draw(win_id, DISP_X,      DISP_Y + 10, sub_buf,   7, 0);
}

/* ------------------------------------------------------------------ */
/* Logika kalkulator                                                   */
/* ------------------------------------------------------------------ */
static void calc_apply_op(void) {
    if (!op) { acc = cur; has_cur = 0; return; }
    switch (op) {
        case '+': acc = acc + cur; break;
        case '-': acc = acc - cur; break;
        case '*': acc = acc * cur; break;
        case '/': if (cur != 0) acc = acc / cur; break;
    }
    has_cur = 0;
}

static void press_digit(int d) {
    just_eq = 0;
    if (!has_cur) { cur = 0; has_cur = 1; }
    if (cur < 99999999L)   /* batas maksimum 8 digit */
        cur = cur * 10 + d;
    draw_display();
}

static void press_op(char o) {
    if (has_cur) calc_apply_op();
    else if (!op) acc = cur;
    op = o;
    just_eq = 0;
    draw_display();
}

static void press_eq(void) {
    if (has_cur) calc_apply_op();
    op = 0;
    just_eq = 1;
    draw_display();
}

static void press_clear(void) {
    acc = 0; cur = 0; has_cur = 0; op = 0; just_eq = 0;
    draw_display();
}

static void press_backspace(void) {
    if (has_cur) {
        cur /= 10;
        if (cur == 0) has_cur = 0;
        draw_display();
    }
}

/* Proses label tombol */
static void handle_label(const char *lbl) {
    char ch = lbl[0];
    if (ch >= '0' && ch <= '9') press_digit(ch - '0');
    else if (ch == '+' || ch == '-' || ch == '*' || ch == '/') press_op(ch);
    else if (ch == '=') press_eq();
    else if (ch == 'C') press_clear();
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */
void _start(void) {
    win_id = win_create(WIN_X, WIN_Y, WIN_W, WIN_H, "Kalkulator");
    if (win_id < 0) exit();

    /* Daftarkan 16 tombol */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int bx = BTN_START_X + c * (BTN_W + BTN_PAD);
            int by = BTN_START_Y + r * (BTN_H + BTN_PAD);
            win_btn_add(win_id, bx, by, BTN_W, BTN_H, btn_labels[r][c]);
        }
    }

    /* Tampilkan display awal */
    draw_display();

    /* Event loop */
    while (1) {
        int ev = win_poll(win_id);
        if (ev == WIN_EVENT_NONE) { yield(); continue; }

        int type = WIN_EV_TYPE(ev);

        if (type == WIN_EVENT_CLOSE) {
            break;

        } else if (type == WIN_EVENT_BTN) {
            int idx = WIN_EV_BTN(ev);
            int r = idx / COLS;
            int c = idx % COLS;
            if (r < ROWS && c < COLS)
                handle_label(btn_labels[r][c]);

        } else if (type == WIN_EVENT_KEY) {
            char ch = WIN_EV_CHAR(ev);
            if (ch >= '0' && ch <= '9') press_digit(ch - '0');
            else if (ch == '+' || ch == '-' || ch == '*' || ch == '/') press_op(ch);
            else if (ch == '\r' || ch == '=') press_eq();
            else if (ch == '\b') press_backspace();
            else if (ch == 27 || ch == 'c' || ch == 'C') press_clear();  /* Esc / C */
        }
    }

    win_destroy(win_id);
    exit();
}
