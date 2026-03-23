#include "pipe.h"

static Pipe pipes[PIPE_MAX];

void pipe_init_all() {
    int i;
    for (i = 0; i < PIPE_MAX; i++) {
        pipes[i].used = 0;
        pipes[i].head = 0;
        pipes[i].tail = 0;
    }
}

int pipe_alloc() {
    int i;
    for (i = 0; i < PIPE_MAX; i++) {
        if (!pipes[i].used) {
            pipes[i].used = 1;
            pipes[i].head = 0;
            pipes[i].tail = 0;
            return i;
        }
    }
    return -1; // semua slot penuh
}

void pipe_free(int id) {
    if (id >= 0 && id < PIPE_MAX)
        pipes[id].used = 0;
}

// Tulis null-terminated string ke ring buffer.
// Setiap pesan diakhiri '\0' sebagai separator di buffer.
int pipe_write(int id, const char *str) {
    if (id < 0 || id >= PIPE_MAX || !pipes[id].used) return -1;
    if (!str) return -1;
    __asm__ volatile ("cli");
    Pipe *p = &pipes[id];
    int written = 0;
    // tulis karakter demi karakter
    while (*str) {
        uint32_t next = (p->tail + 1) % PIPE_BUF;
        if (next == p->head) break; // buffer penuh, hentikan
        p->buf[p->tail] = *str++;
        p->tail = next;
        written++;
    }
    // tulis null terminator sebagai akhir pesan
    uint32_t next = (p->tail + 1) % PIPE_BUF;
    if (next != p->head) {
        p->buf[p->tail] = '\0';
        p->tail = next;
    }
    __asm__ volatile ("sti");
    return written;
}

// Baca satu pesan (sampai '\0') dari ring buffer ke buf.
// buf harus setidaknya PIPE_BUF byte. Return bytes dibaca, 0 jika kosong.
int pipe_read(int id, char *buf) {
    if (id < 0 || id >= PIPE_MAX || !pipes[id].used) return -1;
    if (!buf) return -1;
    __asm__ volatile ("cli");
    Pipe *p = &pipes[id];
    if (p->head == p->tail) { __asm__ volatile ("sti"); return 0; } // kosong
    int i = 0;
    while (p->head != p->tail && i < PIPE_BUF - 1) {
        char c = p->buf[p->head];
        p->head = (p->head + 1) % PIPE_BUF;
        if (c == '\0') break; // akhir pesan
        buf[i++] = c;
    }
    buf[i] = '\0';
    __asm__ volatile ("sti");
    return i;
}
