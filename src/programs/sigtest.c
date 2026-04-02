#include "lib.h"

/* sigtest.c -- Fondasi T: demo signal & exit code */
void _start() {
    int child = fork();

    if (child == 0) {
        print("[sigtest] anak: tidur 10 detik...\n");
        sleep_ms(10000);
        print("[sigtest] anak: selesai tidur (tidak seharusnya!)\n");
        exit_code(0);
    } else if (child > 0) {
        char buf[16];
        print("[sigtest] induk: anak pid=");
        itoa((unsigned int)child, buf);
        print(buf); print("\n");
        sleep_ms(500);
        print("[sigtest] induk: mengirim SIGTERM ke anak...\n");
        kill(child, SIGTERM);
        int ec = waitpid_ex(child);
        print("[sigtest] induk: exit code=");
        itoa((unsigned int)ec, buf);
        print(buf);
        if (ec == 143)
            print(" (OK: 128+SIGTERM=143)\n");
        else
            print(" (mismatch!)\n");
        exit_code(0);
    } else {
        print("[sigtest] fork() gagal!\n");
        exit_code(1);
    }
}
