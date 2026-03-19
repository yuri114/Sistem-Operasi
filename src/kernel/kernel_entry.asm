[BITS 32]
[EXTERN kernel_main]

extern __bss_start
extern __bss_end

; 1) Zero BSS (termasuk page_directory & page_table)
xor eax, eax
mov edi, __bss_start
mov ecx, __bss_end
sub ecx, edi
rep stosb

; 2) Reload GDT dari kernel sendiri (GDT bootloader di 0x7c78 sudah tertimpa BSS zeroing)
lgdt [kernel_gdt_ptr]

; 3) Far jump untuk reload CS dari kernel GDT
jmp 0x08:flush_cs

flush_cs:
    mov ax, 0x10    ; data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

call kernel_main
jmp $

; ===== Kernel GDT (sama seperti GDT bootloader, tapi disimpan di .text section) =====
kernel_gdt:
    ; Entry 0: Null descriptor (wajib)
    dq 0

    ; Entry 1: Code segment (selector = 0x08)
    dw 0xFFFF           ; limit [0:15]
    dw 0x0000           ; base  [0:15]
    db 0x00             ; base  [16:23]
    db 10011010b        ; access: present, ring0, code, readable
    db 11001111b        ; flags: 32-bit, 4KB granularity
    db 0x00             ; base  [24:31]

    ; Entry 2: Data segment (selector = 0x10)
    dw 0xFFFF           ; limit [0:15]
    dw 0x0000           ; base  [0:15]
    db 0x00             ; base  [16:23]
    db 10010010b        ; access: present, ring0, data, writable
    db 11001111b        ; flags: 32-bit, 4KB granularity
    db 0x00             ; base  [24:31]
kernel_gdt_end:

kernel_gdt_ptr:
    dw kernel_gdt_end - kernel_gdt - 1   ; ukuran GDT (bytes - 1)
    dd kernel_gdt                          ; alamat GDT