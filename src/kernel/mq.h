/* mq.h — Message Queue per-task (Tahap L)
 *
 * Setiap task punya mailbox (inbox) berisi hingga MQ_MAX_MSGS pesan.
 * Pengirim menaruh pesan ke mailbox penerima; penerima ambil FIFO.
 * Tidak butuh shared memory — semua di kernel space.
 */
#ifndef MQ_H
#define MQ_H
#include <stdint.h>

#define MQ_MAX_MSGS  8    /* max pesan tertunda per task              */
#define MQ_MSG_SIZE  56   /* max payload bytes per pesan              */

typedef struct {
    uint8_t  used;               /* 1 = slot terpakai                 */
    int      from;               /* task_id pengirim                  */
    uint8_t  len;                /* panjang payload sebenarnya        */
    uint8_t  data[MQ_MSG_SIZE];  /* payload pesan                     */
} MqMsg;

void mq_init(void);

/* Kirim pesan ke dst_pid. Kembalikan 0 sukses, -1 gagal. */
int mq_send(int dst_pid, const uint8_t *data, int len, int from_pid);

/* Terima pesan pertama di inbox dst_pid.
 * Kembalikan panjang payload (>0), 0 jika tidak ada pesan. */
int mq_recv(int dst_pid, uint8_t *buf, int max_len, int *from_pid_out);

/* Jumlah pesan pending di inbox pid */
int mq_pending(int pid);

#endif /* MQ_H */
