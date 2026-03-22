#include "lib.h"

// hello.c — penerima: polling queue dan cetak setiap pesan yang masuk
void _start() {
    print("[receiver] menunggu pesan dari queue...\n");

    char buf[64];
    int total = 0;

    // coba ambil sampai 8 pesan (maks queue)
    int i;
    for (i = 0; i < 8; i++) {
        if (msg_recv(buf)) {
            print("[receiver] terima: ");
            print(buf);
            print("\n");
            total++;
        }
    }

    if (total == 0) {
        print("[receiver] queue kosong — jalankan 'exec sender' dulu!\n");
    } else {
        print("[receiver] selesai, total pesan: ");
        print_int(total);
        print("\n");
    }

    exit();
}