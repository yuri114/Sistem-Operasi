[BITS 32]               ; kita sudah di Protected Mode (32-bit)
[EXTERN kernel_main]    ; beritahu NASM: kernel_main didefinisikan di file lain (C)
call kernel_main        ; panggil fungsi kernel_main() dari kernel.C
jmp $                   ; $ = alamat intruksi ini sendiri
                        ; jmp $ = infinite loop (kalau kernel_main return)