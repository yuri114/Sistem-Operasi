[BITS 16]
[ORG 0x7c00]
KERNEL_OFFSET equ 0x8000    ; kernel dimuat di sini (0x1000 berbahaya: DISK_LOAD bisa menimpa bootloader 0x7C00)

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
    mov dh, KERNEL_SECTORS      ; jumlah sektor kernel (dihitung otomatis saat build)
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
    ; DL=drive, DH=total sektor — muat ke flat 0x8000 via INT 13h/42h (LBA)
    ; LBA 0 = sektor 1 (bootloader), LBA 1 = sektor 2 (awal kernel)
    push cx
    push si

    xor cx, cx              ; cx = sektor yang sudah dibaca

.dl_batch:
    mov al, dh
    sub al, cl              ; al = sisa sektor (8-bit, max ~200)
    jbe .dl_done

    cmp al, 63              ; baca maks 63 sektor per batch (aman di semua BIOS)
    jbe .dl_fit
    mov al, 63
.dl_fit:
    ; isi DAP sebelum setiap batch
    mov byte [dap+2], al    ; jumlah sektor batch ini
    mov byte [dap+3], 0

    ; buffer: segment = 0x800 + cx*32  → flat = segment*16 = 0x8000 + cx*512
    push ax
    mov ax, cx
    shl ax, 5               ; ax = cx * 32
    add ax, 0x0800          ; ax = segment tujuan
    mov [dap+6], ax         ; segment buffer
    mov word [dap+4], 0     ; offset buffer = 0
    ; LBA = 1 + cx  (kernel mulai dari LBA 1)
    mov ax, cx
    inc ax
    mov [dap+8], ax         ; LBA [15:0]
    mov word [dap+10], 0    ; LBA [31:16]
    pop ax                  ; al = jumlah sektor yang diminta batch ini

    push ax
    mov ah, 0x42            ; Extended Read
    mov si, dap             ; DS:SI → DAP
    int 0x13
    pop ax
    jc disk_error           ; carry set = error

    xor ah, ah
    add cx, ax              ; cx += sektor yang dibaca
    jmp .dl_batch

.dl_done:
    pop si
    pop cx
    ret

; Disk Address Packet (DAP) untuk INT 13h/42h — 16 byte
dap:
    db 0x10, 0              ; ukuran DAP=16, reserved
    dw 0                    ; jumlah sektor (diisi tiap batch)
    dw 0, 0                 ; offset:segment buffer (diisi tiap batch)
    dd 0, 0                 ; LBA 64-bit (hanya 32-bit bawah yang dipakai)

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