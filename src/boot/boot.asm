[BITS 16]
[ORG 0x7c00]
KERNEL_OFFSET equ 0x1000    ; kernel akan dimuat di alamat ini

start:
    xor ax, ax              ; ax = 0
    mov ds, ax              ; data segment =  0
    mov es, ax              ; extra segment = 0
    mov ss, ax              ; stack segment = 0
    mov sp, 0x7c00          ; stack pointer dimulai tepat sebelum bootloader
    mov si, msg             ; arahkan SI ke alamat teks yang mau dicetak
    call print_string
    ; simpan boot drive dari BIOS (BIOS set DL sebelum lompat ke bootloader)
    mov [boot_drive], dl

    ; load kernel dari disk ke memory
    mov bx, KERNEL_OFFSET       ; ES:BX = alamat tujuan (0x0000:0x1000)
    mov dh, 40                  ; baca 40 sektor (20KB, lebih dari cukup)
    mov dl, [boot_drive]        ; gunakan drive number dari BIOS
    call disk_load
    cli                     ; matikan semua interrupt
    call switch_to_pm
print_string:
    mov ah, 0x0E            ; fungsi BIOS teletype output
    mov bh, 0               ; halaman video 0
.ps_loop:
    lodsb                   ; load byte dari [SI] ke AL, lalu SI++
    cmp al, 0               ; apakah ini karakter null (akhir string)?
    je .ps_done             ; kalau ya, selesai
    int 0x10                ; panggil BIOS untuk print karakter di AL
    jmp .ps_loop            ; ulangi untuk karakter berikutnya
.ps_done:
    ret                     ; kembali ke pemanggil

disk_load:
    ; input DL = drive, DH = jumlah sektor, BX = alamat tujuan
    push dx                 ; simpan DX (kita butuh DH nanti)

    mov ah, 0x02            ; fungsi BIOS: read sectors
    mov al, dh              ; jumlah sektor yang dibaca
    mov ch, 0               ; cylinder 0
    mov cl, 2               ; mulai dari sektor 2 (sektor 1 = bootloader)
    mov dh, 0               ; head 0
    int 0x13                ; panggil BIOS disk service
    jc disk_error           ; jika carry flag set = error
    pop dx
    cmp al, dh              ; apakah sektor yang terbaca = yang diminta?
    jne disk_error
    ret
disk_error:
    mov si, disk_err_msg
    call print_string
.halt:
    hlt
    jmp .halt

disk_err_msg:
    db 'Disk read error!', 0x0D, 0x0A, 0

boot_drive:
    db 0

msg:
    db 'Hello from MyOS!', 0x0D, 0x0A, 0

; =============================
; GDT (Global Descriptor Table)
; =============================

gdt_start:

gdt_null:               ; Entry 0: Null Descriptor (wajib, selalu 0 semua)
    dd 0x00000000
    dd 0x00000000

gdt_code:               ; Entry 1: Code segment
    dw 0xFFFF           ; limit [0:15]  = 0xFFFF
    dw 0x0000           ; base  [0:15]  = 0x0000
    db 0x00             ; base  [16:23] = 0x00
    db 10011010b        ; access byte: present=1, ring=0, code segment, readable
    db 11001111b        ; flags + limit [16:19]: 32-bit,4KB granularity
    db 0x00             ; base [24:31]  = 0x00

gdt_data:               ; Entry 2: Data Segment
    dw 0xFFFF           ; limit [0:15]  = 0xFFFF
    dw 0x0000           ; base  [0:15]  = 0x0000
    db 0x00             ; base  [16:23] = 0x00
    db 10010010b        ; access byte: present=1, ring=0, data segment, writable
    db 11001111b        ; flags + limit [16:19]:32-bit, 4KB granularity
    db 0x00             ; base [24:31]  = 0x00

gdt_end:

; GDT Descriptor - struktur yang di-load ke CPU via 'lgdt'
gdt_descriptor:
    dw gdt_end - gdt_start - 1      ; ukuran GDT (dalam bytes, dikurangi 1)
    dd gdt_start                    ; alamat GDT di memori

; konstanta selector - offset entry di GDT
CODE_SEG equ gdt_code - gdt_start   ; = 0x08
DATA_SEG equ gdt_data - gdt_start   ; = 0x10

switch_to_pm:
    lgdt [gdt_descriptor]           ; load GDT ke CPU

    mov eax, cr0                    ; baca nilai register CR0
    or eax, 1                       ; set bit 0 (PE = Protection Enable)
    mov cr0, eax                    ; tulis balik, CPU sekarang masuk Protected mode

    jmp CODE_SEG:pm_entry           ; far jump: flush pipeline CPU + lompat ke 32-bit code

[BITS 32]                           ; mulai dari sini: kode 32-bit
pm_entry:
    ; update semua segment register ke DATA_SEG
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000                ; setup stack baru untuk 32-bit (di atas 512KB)

    call KERNEL_OFFSET              ; lompat ke kernel

    ; untuk sekarang: loop selamanya
.halt32:
    hlt
    jmp .halt32

times 510 - ($ - $$) db 0
dw 0xAA55