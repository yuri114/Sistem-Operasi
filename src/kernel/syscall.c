#include "syscall.h"

extern void print(const char *str); // dari kernel.c
void syscall_handler(uint32_t eax, uint32_t ebx) {
    if (eax == SYS_PRINT){
        print((const char*)ebx); //ebx berisi pointer ke string yang akan dicetak
    }
    //SYS_GETKEY dan SYS_EXIT belum diimplementasi, bisa ditambahkan nanti
}
