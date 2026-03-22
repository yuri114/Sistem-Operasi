#ifndef PAGING_H
#define PAGING_H
#include <stdint.h>

void paging_init();
uint32_t paging_get_cr0();
extern uint32_t page_directory[1024]; //deklarasi page directory agar bisa diakses dari shell.c untuk cek status paging

#endif