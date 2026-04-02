/* keyboard.c — Driver keyboard PS/2 (IRQ1), buffer ring, tab/arrow support */
#include <stdint.h>
#include "keyboard.h"
#include "task.h"
#include "pic.h"

#define KEYBOARD_DATA_PORT  0x60
#define KEY_BUFFER_SIZE     64

static char key_buffer[KEY_BUFFER_SIZE];
static int  key_head = 0;   /* indeks baca */
static int  key_tail = 0;   /* indeks tulis */

/* -------------------------------------------------------------------
 * Buffer ring
 * ------------------------------------------------------------------- */

static void key_push(char c) {
    int next = (key_tail + 1) % KEY_BUFFER_SIZE;
    if (next != key_head) {     /* abaikan jika buffer penuh */
        key_buffer[key_tail] = c;
        key_tail = next;
    }
}

/* Ambil satu karakter dari buffer; kembalikan 0 jika kosong. */
char keyboard_getchar() {
    if (key_head == key_tail) return 0;
    char c    = key_buffer[key_head];
    key_head  = (key_head + 1) % KEY_BUFFER_SIZE;
    return c;
}

/* -------------------------------------------------------------------
 * Tabel scancode → ASCII (index = scancode, 0 = tidak ada karakter)
 * ------------------------------------------------------------------- */
static char scancode_table[] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8',   /* 0x00–0x09 */
    '9', '0', '-', '=', 0,   0,   'q', 'w', 'e', 'r',   /* 0x0A–0x13 */
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,   0,     /* 0x14–0x1D */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',   /* 0x1E–0x27 */
    '\'','`', 0,  '\\','z', 'x', 'c', 'v', 'b', 'n',   /* 0x28–0x31 */
    'm', ',', '.', '/',  0,  '*',  0,  ' '               /* 0x32–0x39 */
};

static char scancode_shift_table[] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*',   /* 0x00–0x09 */
    '(', ')', '_', '+', 0,   0,   'Q', 'W', 'E', 'R',   /* 0x0A–0x13 */
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0,   0,     /* 0x14–0x1D */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',   /* 0x1E–0x27 */
    '"', '~', 0,  '|', 'Z', 'X', 'C', 'V', 'B', 'N',   /* 0x28–0x31 */
    'M', '<', '>', '?',  0,  '*',  0,  ' '              /* 0x32–0x39 */
};

/* -------------------------------------------------------------------
 * Handler IRQ1
 *
 * Hanya buffering — tidak menggambar ke layar.
 * Shell polling loop di kernel_main yang melakukan rendering.
 * ------------------------------------------------------------------- */
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static uint8_t extended      = 0;   /* flag prefix 0xE0 (arrow keys, dll)  */
static uint8_t shift_pressed = 0;   /* 1 = Left/Right Shift sedang ditekan */
static uint8_t ctrl_pressed  = 0;   /* 1 = Left/Right Ctrl sedang ditekan  */
static uint8_t alt_pressed   = 0;   /* 1 = Left Alt sedang ditekan         */
static uint8_t capslock_on   = 0;   /* 1 = Caps Lock aktif (toggle)        */

/* F-T: tid proses foreground untuk dikirim SIGINT saat Ctrl+C */
static int fg_task_pid = -1;
void keyboard_set_fg_pid(int pid) { fg_task_pid = pid; }

int keyboard_ctrl_pressed()    { return (int)ctrl_pressed; }
int keyboard_alt_pressed()     { return (int)alt_pressed; }
int keyboard_capslock_state()  { return (int)capslock_on; }
int keyboard_has_char()        { return key_head != key_tail; }

/* tid task yang menunggu karakter keyboard (-1 = tidak ada) */
static int kwaiter = -1;

void keyboard_set_waiter(int tid) { kwaiter = tid; }

void keyboard_handler() {
    uint8_t sc = inb(KEYBOARD_DATA_PORT);
    int wake = -1;  /* tid yang akan dibangunkan setelah proses, √ jika ada char baru */

    /* 0xE0 = extended scancode prefix */
    if (sc == 0xE0) { extended = 1; return; }

    /* Shift release */
    if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; extended = 0; return; }
    /* Ctrl release: Left=0x9D */
    if (sc == 0x9D) { ctrl_pressed = 0; extended = 0; return; }
    /* Alt release: Left=0xB8 */
    if (sc == 0xB8) { alt_pressed = 0; extended = 0; return; }

    /* Key release lainnya (bit 7=1) */
    if (sc & 0x80) { extended = 0; return; }

    /* Shift press */
    if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; return; }
    /* Ctrl press: Left=0x1D */
    if (sc == 0x1D) { ctrl_pressed = 1; return; }
    /* Alt press: Left=0x38 */
    if (sc == 0x38) { alt_pressed = 1; return; }
    /* Caps Lock toggle: 0x3A */
    if (sc == 0x3A) { capslock_on ^= 1; return; }

    /* Extended key: arrow keys, Delete */
    if (extended) {
        extended = 0;
        if      (sc == 0x48) key_push(KEY_UP);      /* Arrow Up    */
        else if (sc == 0x50) key_push(KEY_DOWN);    /* Arrow Down  */
        else if (sc == 0x4B) key_push(KEY_LEFT);    /* Arrow Left  */
        else if (sc == 0x4D) key_push(KEY_RIGHT);   /* Arrow Right */
        else if (sc == 0x53) key_push(KEY_DELETE);  /* Delete      */
        else if (sc == 0x9D) ctrl_pressed = 0;      /* Right Ctrl release */
        return;
    }

    /* Tab */
    if (sc == 0x0F) { key_push(KEY_TAB); wake = kwaiter; kwaiter = -1; if (wake >= 0) task_unblock(wake); return; }

    /* F1-F10: scancode 0x3B-0x44 */
    if (sc >= 0x3B && sc <= 0x44) { key_push((char)(KEY_F1 + (sc - 0x3B))); wake = kwaiter; kwaiter = -1; if (wake >= 0) task_unblock(wake); return; }

    if      (sc == 0x0E) key_push('\b');    /* Backspace */
    else if (sc == 0x1C) key_push('\n');    /* Enter     */
    else if (sc == 0x01) key_push(0x1B);   /* Escape    */
    else if (sc < sizeof(scancode_table)) {
        char c = shift_pressed ? scancode_shift_table[sc] : scancode_table[sc];
        /* Caps Lock: balik huruf kecil/besar */
        if (c && capslock_on) {
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            else if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        }
        /* F-T: Ctrl+C → kirim SIGINT ke proses foreground (sebelum kode kontrol umum) */
        if (ctrl_pressed && (sc == 0x2E)) {  /* 0x2E = scancode 'c' */
            if (fg_task_pid >= 0) {
                task_send_signal(fg_task_pid, 2);  /* SIGINT = 2 */
                fg_task_pid = -1;
            }
            return;  /* jangan masukkan karakter ke buffer */
        }
        /* Ctrl+huruf: kode kontrol 0x01-0x1A */
        if (c && ctrl_pressed) {
            if (c >= 'a' && c <= 'z')      c = (char)(c - 'a' + 1);
            else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
        }
        if (c) key_push(c);
    }
    /* Bangunkan task yang menunggu karakter keyboard */
    wake = kwaiter;
    kwaiter = -1;
    if (wake >= 0) task_unblock(wake);
}

