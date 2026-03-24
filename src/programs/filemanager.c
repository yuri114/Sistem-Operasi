/*
 * filemanager.c — File Manager berbasis window GUI
 *
 * Fitur:
 *  - Daftar file di filesystem kernel dengan scrollbar
 *  - Double-click atau Exec: jalankan program
 *  - Edit: buka notepad
 *  - Rename: ganti nama file (dialog mini)
 *  - Del: hapus file
 *  - Arrow Up/Down: navigasi daftar
 *  - Refresh, Close
 */
#include "lib.h"

/* ---- Dimensi window & layout ---- */
#define WIN_W           360
#define WIN_H           270
#define LIST_Y           24   /* y awal daftar file di koordinat konten */
#define LIST_H           10   /* tinggi per baris */
#define LIST_VISIBLE     18   /* baris yang terlihat */
#define MAX_FILES        16
#define SCROLL_X        348   /* CA_W - 10 = 358 - 10 */
#define SCROLL_W         10   /* lebar scrollbar */
#define SCROLL_TRACK_H  (LIST_VISIBLE * LIST_H)   /* 180 */
#define DBLCLICK_TICKS    5   /* ~275ms pada 18.2 tick/detik */

/* ---- Indeks tombol ---- */
#define BTN_EXEC     0
#define BTN_EDIT     1
#define BTN_RENAME   2
#define BTN_DEL      3
#define BTN_REFRESH  4
#define BTN_CLOSE    5

/* ---- State ---- */
static char file_names[MAX_FILES][32];
static int  file_count;
static int  selected;
static int  top_row;
static unsigned int last_click_tick;
static int  last_click_row;

/* ---- Utilitas string lokal ---- */
static int fm_len(const char *s) { int n=0; while(s[n]) n++; return n; }
static void fm_cpy(char *d, const char *s, int max) {
    int i=0; while(i<max-1 && s[i]) { d[i]=s[i]; i++; } d[i]='\0';
}

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

/* ---- Scrollbar ---- */
static void draw_scrollbar(int id) {
    win_fill_rect(id, SCROLL_X, LIST_Y, SCROLL_W, SCROLL_TRACK_H, GFX_DGRAY);
    win_fill_rect(id, SCROLL_X, LIST_Y, 1, SCROLL_TRACK_H, GFX_BLACK);
    if (file_count <= LIST_VISIBLE) {
        win_fill_rect(id, SCROLL_X+1, LIST_Y, SCROLL_W-1, SCROLL_TRACK_H, GFX_LGRAY);
    } else {
        int th = SCROLL_TRACK_H * LIST_VISIBLE / file_count;
        if (th < 6) th = 6;
        int ty = LIST_Y + (SCROLL_TRACK_H - th) * top_row / (file_count - LIST_VISIBLE);
        win_fill_rect(id, SCROLL_X+1, ty, SCROLL_W-1, th, GFX_LGRAY);
    }
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
    draw_scrollbar(id);
}

/* ---- Entry point ---- */
void _start(void) {
    file_count = 0;
    selected   = 0;
    top_row    = 0;
    last_click_tick = 0;
    last_click_row  = -1;

    int id = win_create((640 - WIN_W) / 2,
                        (480 - 16 - WIN_H) / 2,
                        WIN_W, WIN_H, "File Manager");
    if (id < 0) exit();

    /* Tombol */
    win_btn_add(id,  4,            2, 44, 18, "Exec");
    win_btn_add(id,  52,           2, 40, 18, "Edit");
    win_btn_add(id,  96,           2, 52, 18, "Rename");
    win_btn_add(id, 152,           2, 36, 18, "Del");
    win_btn_add(id, WIN_W-2-112,   2, 52, 18, "Refresh");
    win_btn_add(id, WIN_W-2- 56,   2, 52, 18, "Close");

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

            if (b == BTN_RENAME) {
                if (selected >= 0 && selected < file_count && fm_len(file_names[selected]) > 0) {
                    /* Dialog rename: window kecil untuk input nama baru */
                    int dw=300, dh=80;
                    int did = win_create((640-dw)/2, (480-16-dh)/2, dw, dh, "Rename");
                    if (did >= 0) {
                        win_draw(did, 4, 8, "Nama baru:", GFX_WHITE, GFX_BLACK);
                        /* Input sederhana: buffer + event loop mini */
                        static char new_name[32];
                        int nn_len = 0;
                        new_name[0] = '\0';
                        win_btn_add(did, 4,      48, 56, 18, "OK");
                        win_btn_add(did, 64,     48, 56, 18, "Cancel");
                        /* Pre-fill dengan nama lama */
                        fm_cpy(new_name, file_names[selected], 32);
                        nn_len = fm_len(new_name);
                        int done = 0;
                        while (!done) {
                            /* Render nama saat ini di dialog */
                            char disp[36];
                            fm_cpy(disp, new_name, 32);
                            int dl = fm_len(disp);
                            disp[dl]='|'; disp[dl+1]='\0';
                            win_clear(did, GFX_BLACK);
                            win_draw(did, 4, 8,  "Nama baru:", GFX_WHITE,  GFX_BLACK);
                            win_draw(did, 4, 22, disp,         GFX_YELLOW, GFX_BLACK);
                            int dev = win_poll(did);
                            if (dev == WIN_EVENT_NONE) { yield(); continue; }
                            int dt = WIN_EV_TYPE(dev);
                            if (dt == WIN_EVENT_CLOSE) { done = 2; }
                            else if (dt == WIN_EVENT_KEY) {
                                char dc = WIN_EV_CHAR(dev);
                                if (dc == '\r' || dc == '\n') { done = 1; }
                                else if (dc == '\b') { if (nn_len>0) { nn_len--; new_name[nn_len]='\0'; } }
                                else if (dc >= 32 && dc < 127 && nn_len < 31) {
                                    new_name[nn_len++] = dc; new_name[nn_len] = '\0';
                                }
                            } else if (dt == WIN_EVENT_BTN) {
                                int db = WIN_EV_BTN(dev);
                                if (db == 0) done = 1;  /* OK */
                                else         done = 2;  /* Cancel */
                            }
                        }
                        win_destroy(did);
                        if (done == 1 && nn_len > 0) {
                            /* Rename: baca isi file lama ke buffer lokal, tulis dengan
                             * nama baru, hapus file lama. fs_read menyalin langsung ke
                             * buffer userspace sehingga fs_write dapat menerimanya. */
                            static char rename_buf[16384];
                            int rsz = fs_read(file_names[selected], rename_buf, sizeof(rename_buf));
                            if (rsz >= 0) {
                                rename_buf[rsz] = '\0';
                                fs_write(new_name, rename_buf);
                            }
                            fs_delete(file_names[selected]);
                            refresh_list();
                            do_render(id);
                        }
                    }
                }
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

        if (t == WIN_EVENT_KEY) {
            char c = WIN_EV_CHAR(ev);
            if (c == '\x01') {  /* ↑ */
                if (selected > 0) {
                    selected--;
                    if (selected < top_row) top_row = selected;
                    do_render(id);
                }
            } else if (c == '\x02') {  /* ↓ */
                if (selected < file_count - 1) {
                    selected++;
                    if (selected >= top_row + LIST_VISIBLE)
                        top_row = selected - LIST_VISIBLE + 1;
                    do_render(id);
                }
            } else if (c == '\r' || c == '\n') {
                if (selected >= 0 && selected < file_count && fm_len(file_names[selected]) > 0)
                    exec(file_names[selected]);
            }
        }

        if (t == WIN_EVENT_CLICK) {
            int cx, cy;
            win_click_pos(id, &cx, &cy);

            /* Klik di scrollbar */
            if (cx >= SCROLL_X && cy >= LIST_Y && cy < LIST_Y + SCROLL_TRACK_H) {
                if (file_count > LIST_VISIBLE) {
                    int rel = cy - LIST_Y;
                    top_row = rel * (file_count - LIST_VISIBLE) / (SCROLL_TRACK_H - 1);
                    if (top_row > file_count - LIST_VISIBLE)
                        top_row = file_count - LIST_VISIBLE;
                    do_render(id);
                }

            } else if (cy >= LIST_Y) {
                int clicked = top_row + (cy - LIST_Y) / LIST_H;
                if (clicked >= 0 && clicked < file_count) {
                    unsigned int now = get_ticks();
                    if (clicked == last_click_row &&
                        (now - last_click_tick) <= DBLCLICK_TICKS &&
                        fm_len(file_names[clicked]) > 0) {
                        /* Double-click: jalankan program */
                        exec(file_names[clicked]);
                        last_click_row  = -1;
                        last_click_tick = 0;
                    } else {
                        selected       = clicked;
                        last_click_row  = clicked;
                        last_click_tick = now;
                        do_render(id);
                    }
                }
            }
        }
    }

    win_destroy(id);
    exit();
}
