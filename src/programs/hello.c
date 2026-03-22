#include "lib.h"

void _start() {
    print("=== Hello ELF Program ===\n");

    // tulis file ke filesystem kernel
    int ok = fs_write("pesan", "Halo dari program ELF!");
    if (ok) {
        print("fs_write: file 'pesan' berhasil disimpan\n");
    } else {
        print("fs_write: gagal!\n");
    }

    // baca kembali file yang baru ditulis
    const char *isi = fs_read("pesan");
    if (isi) {
        print("fs_read 'pesan': ");
        print(isi);
        print("\n");
    } else {
        print("fs_read: file tidak ditemukan\n");
    }

    // baca file yang tidak ada
    const char *tidak_ada = fs_read("xyz");
    if (!tidak_ada) {
        print("fs_read 'xyz': tidak ditemukan (benar)\n");
    }

    exit();
}