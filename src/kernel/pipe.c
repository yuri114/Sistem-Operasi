#include "pipe.h"
#include "task.h"

static Pipe pipes[PIPE_MAX];

void pipe_init_all() {
    int i;
    for (i = 0; i < PIPE_MAX; i++) {
        pipes[i].used          = 0;
        pipes[i].head          = 0;
        pipes[i].tail          = 0;
        pipes[i].eof           = 0;
        pipes[i].write_refs    = 0;
        pipes[i].reader_waiter = -1;
    }
}

int pipe_alloc() {
    int i;
    for (i = 0; i < PIPE_MAX; i++) {
        if (!pipes[i].used) {
            pipes[i].used          = 1;
            pipes[i].head          = 0;
            pipes[i].tail          = 0;
            pipes[i].eof           = 0;
            pipes[i].write_refs    = 0;
            pipes[i].reader_waiter = -1;
            return i;
        }
    }
    return -1;
}

void pipe_free(int id) {
    if (id >= 0 && id < PIPE_MAX)
        pipes[id].used = 0;
}

void pipe_writer_attach(int id) {
    if (id >= 0 && id < PIPE_MAX && pipes[id].used)
        pipes[id].write_refs++;
}

void pipe_writer_detach(int id) {
    int w;
    if (id < 0 || id >= PIPE_MAX || !pipes[id].used) return;
    __asm__ volatile ("cli");
    if (pipes[id].write_refs > 0) pipes[id].write_refs--;
    if (pipes[id].write_refs == 0) {
        pipes[id].eof = 1;
        /* Bangunkan pembaca yang sedang tunggu agar dapat EOF */
        w = pipes[id].reader_waiter;
        pipes[id].reader_waiter = -1;
        __asm__ volatile ("sti");
        if (w >= 0) task_unblock(w);
    } else {
        __asm__ volatile ("sti");
    }
}

// Tulis null-terminated string ke ring buffer.
// Setiap pesan diakhiri '\0' sebagai separator di buffer.
int pipe_write(int id, const char *str) {
    int w;
    if (id < 0 || id >= PIPE_MAX || !pipes[id].used) return -1;
    if (!str) return -1;
    __asm__ volatile ("cli");
    Pipe *p = &pipes[id];
    int written = 0;
    while (*str) {
        uint32_t next = (p->tail + 1) % PIPE_BUF;
        if (next == p->head) break;
        p->buf[p->tail] = *str++;
        p->tail = next;
        written++;
    }
    uint32_t next = (p->tail + 1) % PIPE_BUF;
    if (next != p->head) {
        p->buf[p->tail] = '\0';
        p->tail = next;
    }
    /* Bangunkan pembaca yang sedang menunggu */
    w = p->reader_waiter;
    p->reader_waiter = -1;
    __asm__ volatile ("sti");
    if (w >= 0) task_unblock(w);
    return written;
}

// Baca satu pesan (sampai '\0') dari ring buffer ke buf.
// Memblokir (true block) sampai data tersedia atau EOF. Return bytes dibaca (0=EOF).
int pipe_read(int id, char *buf) {
    if (id < 0 || id >= PIPE_MAX || !pipes[id].used) return -1;
    if (!buf) return -1;
    /* Tunggu sampai ada data atau EOF */
    while (1) {
        __asm__ volatile ("cli");
        if (pipes[id].head != pipes[id].tail) break;  /* ada data */
        if (pipes[id].eof) {                           /* EOF: tidak ada penulis */
            __asm__ volatile ("sti");
            return 0;
        }
        pipes[id].reader_waiter = task_get_current();
        __asm__ volatile ("sti");
        task_block();  /* tidur sampai pipe_write/detach membangunkan */
        /* F-T: sinyal pending (mis. SIGINT dari Ctrl+C) → batalkan baca */
        task_check_signals();  /* tidak kembali jika sinyal terminating */
    }
    Pipe *p = &pipes[id];
    int i = 0;
    while (p->head != p->tail && i < PIPE_BUF - 1) {
        char c = p->buf[p->head];
        p->head = (p->head + 1) % PIPE_BUF;
        if (c == '\0') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    __asm__ volatile ("sti");
    return i;
}

/* ====== Named Pipe ====== */
static NamedPipe named_pipes[NAMED_PIPE_MAX];

static int np_streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == '\0' && b[i] == '\0';
}
static void np_strcpy(char *dst, const char *src, int n) {
    int i = 0;
    while (src[i] && i < n - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* Buka atau buat named pipe — return pipe_id, -1 jika gagal */
int named_pipe_open(const char *name) {
    int i;
    if (!name) return -1;
    /* Cari yang sudah ada */
    for (i = 0; i < NAMED_PIPE_MAX; i++)
        if (named_pipes[i].used && np_streq(named_pipes[i].name, name))
            return named_pipes[i].pipe_id;
    /* Buat baru */
    for (i = 0; i < NAMED_PIPE_MAX; i++) {
        if (!named_pipes[i].used) {
            int pid = pipe_alloc();
            if (pid < 0) return -1;
            named_pipes[i].used    = 1;
            named_pipes[i].pipe_id = pid;
            np_strcpy(named_pipes[i].name, name, 16);
            return pid;
        }
    }
    return -1;
}

/* Tutup named pipe berdasarkan nama */
void named_pipe_close(const char *name) {
    int i;
    if (!name) return;
    for (i = 0; i < NAMED_PIPE_MAX; i++) {
        if (named_pipes[i].used && np_streq(named_pipes[i].name, name)) {
            pipe_free(named_pipes[i].pipe_id);
            named_pipes[i].used  = 0;
            named_pipes[i].name[0] = '\0';
            return;
        }
    }
}
