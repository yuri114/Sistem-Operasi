#include "ipc.h"

// FIFO ring buffer sederhana
// head = index slot yang akan dibaca berikutnya
// tail = index slot yang akan diisi berikutnya
// kosong jika head == tail
// penuh  jika (tail + 1) % SIZE == head

static char queue[IPC_QUEUE_SIZE][IPC_MSG_LEN];
static int  head = 0;
static int  tail = 0;

void ipc_init() {
    head = 0;
    tail = 0;
}

// salin string maks n-1 karakter, selalu null-terminated
static void ipc_strncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

int ipc_send(const char *msg) {
    int next = (tail + 1) % IPC_QUEUE_SIZE;
    if (next == head) return 0; // queue penuh
    ipc_strncpy(queue[tail], msg, IPC_MSG_LEN);
    tail = next;
    return 1;
}

int ipc_recv(char *buf) {
    if (head == tail) return 0; // queue kosong
    ipc_strncpy(buf, queue[head], IPC_MSG_LEN);
    head = (head + 1) % IPC_QUEUE_SIZE;
    return 1;
}
