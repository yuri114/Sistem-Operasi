#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

#define PIPE_MAX 8    // jumlah pipe serentak
#define PIPE_BUF 256  // ukuran ring buffer per pipe

typedef struct {
    char     buf[PIPE_BUF];  // ring buffer data
    uint32_t head;           // index baca berikutnya
    uint32_t tail;           // index tulis berikutnya
    uint8_t  used;           // slot aktif?
    uint8_t  eof;            // 1 = semua write-end tutup; pembaca dapat EOF
    int8_t   write_refs;     // jumlah fd write-end yang aktif
    int      reader_waiter;  // tid blocked di pipe_read (-1 = tidak ada)
} Pipe;

// Inisialisasi semua slot pipe
void pipe_init_all();

// Alokasi pipe baru — return id (0..PIPE_MAX-1) atau -1 jika penuh
int  pipe_alloc();

// Bebaskan slot pipe
void pipe_free(int id);

// Tulis null-terminated string ke pipe — return bytes ditulis, -1 jika error
int  pipe_write(int id, const char *str);

// Baca satu pesan (sampai '\0') dari pipe ke buf — return bytes dibaca (0=EOF/kosong, -1=error)
int  pipe_read(int id, char *buf);

// Referensi write-end: incr saat redirect_out/vfs_pipe, decr saat close
void pipe_writer_attach(int id);
void pipe_writer_detach(int id);  /* set eof jika write_refs turun ke 0 */

/* ====== Named Pipe ====== */
#define NAMED_PIPE_MAX 8

typedef struct {
    char name[16];  // nama unik
    int  pipe_id;   // id pipe anonim yang terhubung
    int  used;      // slot aktif?
} NamedPipe;

// Buka (atau buat) named pipe dengan nama tertentu — return pipe_id, atau -1 jika gagal
int named_pipe_open(const char *name);

// Tutup named pipe berdasarkan nama (bebaskan jika no reference)
void named_pipe_close(const char *name);

#endif
