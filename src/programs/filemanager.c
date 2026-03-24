/*
 * filemanager.c — File Manager sederhana berbasis window GUI
 *
 * Fitur:
 *  - Daftar file di filesystem kernel
 *  - Exec: jalankan file yang dipilih sebagai program
 *  - Edit: buka notepad
 *  - Del: hapus file yang dipilih
 *  - Refresh: perbarui daftar
 *  - Klik baris untuk memilih file
 *
 * Constraint: WIN_TEXTBUF=32 → 1 win_clear + 18 baris daftar = 19 entries ✓
 */
#include "lib.h"

/* ---- Dimensi window & layout ---- */
#define WIN_W           360
#define WIN_H           270
#define LIST_Y           24   /* y awal daftar file di koordinat konten */
#define LIST_H           10   /* tinggi per baris */
#define LIST_VISIBLE     18   /* baris daftar yang terlihat sekaligus */
#define MAX_FILES        16

/* ---- Indeks tombol ---- */
#define BTN_EXEC     0
#define BTN_EDIT     1
#define BTN_DEL      2
#define BTN_REFRESH  3
#define BTN_CLOSE    4

/* ---- State ---- */
static char file_names[MAX_FILES][32];
static int  file_count;
static int  selected;
static int  top_row;

/* ---- Utilitas string lokal ---- */
static int fm_len(const char *s) { int n=0; while(s[n]) n++; return n; }

/* ---- Refresh daftar file ---- */
static void refresh_list(void) {
    static char list_buf[512];
    int i;

    file_count = fs_list(list_buf, 512);
    if (file_count < 0) file_count = 0;

    /* parse buffer: nama dipisah '\n' */
    int pos = 0, fi = 0;
    while (list_buf[pos] && fi < MAX_FILES) {
        int ni = 0;
        while (list_buf[pos] && list_buf[pos] != '\n' && ni < 31) {
            file_names[fi][ni++] = list_buf[pos++];
        }
        file_names[fi][ni] = '\0';
        if (ni > 0) fi++;
        if (list_buf[pos] == '\n') pos++;
    }
    file_count = fi;

    /* bersihkan sisa slot */
    for (i = fi; i < MAX_FILES; i++) file_names[i][0] = '\0';

    /* clamp selected */
    if (selected >= file_count) selected = file_count - 1;
    if (selected < 0)           selected = 0;
}

/* ---- Render ---- */
static void do_render(int id) {
    int i;
    win_clear(id, GFX_BLACK);

    for (i = 0; i < LIST_VISIBLE; i++) {
        int fi = top_row + i;
        int ry = LIST_Y + i * LIST_H;
        if (fi < file_count) {
            int is_sel = (fi == selected) ? 1 : 0;
            win_draw(id, 4, ry,
                     file_names[fi],
                     GFX_WHITE,
                     is_sel ? GFX_BLUE : GFX_BLACK);
        }
    }
}

/* ---- Entry point ---- */
void _start(void) {
    file_count = 0;
    selected   = 0;
    top_row    = 0;

    int id = win_create((640 - WIN_W) / 2,
                        (480 - 16 - WIN_H) / 2,
                        WIN_W, WIN_H, "File Manager");
    if (id < 0) exit();

    /* Baris tombol kiri: Exec | Edit | Del */
    win_btn_add(id,  4,           2, 44, 18, "Exec");
    win_btn_add(id,  52,          2, 40, 18, "Edit");
    win_btn_add(id,  96,          2, 36, 18, "Del");

    /* Baris tombol kanan: Refresh | Close */
    win_btn_add(id,  WIN_W-2-112, 2, 52, 18, "Refresh");
    win_btn_add(id,  WIN_W-2- 56, 2, 52, 18, "Close");

    refresh_list();
    do_render(id);

    for (;;) {
        int ev = win_poll(id);
        if (ev == WIN_EVENT_NONE) { yield(); continue; }

        int t = WIN_EV_TYPE(ev);

        if (t == WIN_EVENT_CLOSE) break;

        if (t == WIN_EVENT_BTN) {
            int b = WIN_EV_BTN(ev);

            if (b == BTN_EXEC) {
                if (selected >= 0 && selected < file_count && fm_len(file_names[selected]) > 0)
                    exec(file_names[selected]);
            }

            if (b == BTN_EDIT) {
                exec("notepad");
            }

            if (b == BTN_DEL) {
                if (selected >= 0 && selected < file_count && fm_len(file_names[selected]) > 0) {
                    fs_delete(file_names[selected]);
                    refresh_list();
                    do_render(id);
                }
            }

            if (b == BTN_REFRESH) {
                refresh_list();
                do_render(id);
            }

            if (b == BTN_CLOSE) break;
        }

        if (t == WIN_EVENT_CLICK) {
            int cx, cy;
            win_click_pos(id, &cx, &cy);
            (void)cx;
            if (cy >= LIST_Y) {
                int clicked = top_row + (cy - LIST_Y) / LIST_H;
                if (clicked >= 0 && clicked < file_count) {
                    selected = clicked;
                    do_render(id);
                }
            }
        }
    }

    win_destroy(id);
    exit();
}
