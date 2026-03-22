#include "lib.h"

// sender.c — kirim 3 pesan ke queue, lalu selesai
void _start() {
    print("[sender] mulai mengirim pesan...\n");

    msg_send("pesan-1: halo dari sender!");
    print("[sender] pesan 1 dikirim\n");

    msg_send("pesan-2: IPC bekerja!");
    print("[sender] pesan 2 dikirim\n");

    msg_send("pesan-3: selamat belajar OS!");
    print("[sender] pesan 3 dikirim\n");

    print("[sender] selesai\n");
    exit();
}
