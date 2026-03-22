#ifndef IPC_H
#define IPC_H
#include <stdint.h>

#define IPC_QUEUE_SIZE 8    // maksimal 8 pesan menunggu
#define IPC_MSG_LEN    64   // maksimal 64 karakter per pesan

void ipc_init();
int  ipc_send(const char *msg);          // kirim pesan ke queue, return 1 sukses / 0 penuh
int  ipc_recv(char *buf);               // ambil pesan dari queue ke buf, return 1 ada / 0 kosong

#endif
