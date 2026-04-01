/* mq.c — Message Queue implementation (Tahap L) */
#include "mq.h"
#include "task.h"
#include <stdint.h>

/* Mailbox global: mailbox[task_id][slot] */
static MqMsg mailbox[MAX_TASKS][MQ_MAX_MSGS];

void mq_init(void)
{
    int i, j;
    for (i = 0; i < MAX_TASKS; i++)
        for (j = 0; j < MQ_MAX_MSGS; j++)
            mailbox[i][j].used = 0;
}

int mq_send(int dst_pid, const uint8_t *data, int len, int from_pid)
{
    int j, k;
    if (dst_pid < 0 || dst_pid >= MAX_TASKS) return -1;
    if (!task_is_used(dst_pid)) return -1;
    if (len > MQ_MSG_SIZE) len = MQ_MSG_SIZE;
    if (len < 0) len = 0;
    for (j = 0; j < MQ_MAX_MSGS; j++) {
        if (!mailbox[dst_pid][j].used) {
            mailbox[dst_pid][j].used = 1;
            mailbox[dst_pid][j].from = from_pid;
            mailbox[dst_pid][j].len  = (uint8_t)len;
            for (k = 0; k < len; k++) mailbox[dst_pid][j].data[k] = data[k];
            return 0;
        }
    }
    return -1;  /* mailbox penuh */
}

int mq_recv(int dst_pid, uint8_t *buf, int max_len, int *from_pid_out)
{
    int j, k, len;
    if (dst_pid < 0 || dst_pid >= MAX_TASKS) return 0;
    for (j = 0; j < MQ_MAX_MSGS; j++) {
        if (mailbox[dst_pid][j].used) {
            len = mailbox[dst_pid][j].len;
            if (len > max_len) len = max_len;
            for (k = 0; k < len; k++) buf[k] = mailbox[dst_pid][j].data[k];
            if (from_pid_out) *from_pid_out = mailbox[dst_pid][j].from;
            mailbox[dst_pid][j].used = 0;
            return len;
        }
    }
    return 0;  /* tidak ada pesan */
}

int mq_pending(int pid)
{
    int j, count = 0;
    if (pid < 0 || pid >= MAX_TASKS) return 0;
    for (j = 0; j < MQ_MAX_MSGS; j++)
        if (mailbox[pid][j].used) count++;
    return count;
}
