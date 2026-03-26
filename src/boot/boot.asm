; ================================================================
; boot.asm — MyOS Stage-1 Bootloader (512 byte MBR)
;
; Alur kerja:
;   1. Inisialisasi segment & stack di Real Mode
;   2. Load kernel dari disk ke 0x8000 via BIOS INT 13h/42h (LBA)
;   3. Aktifkan Protected Mode (PE), reload GDT kernel
;   4. Lompat ke kernel_entry.asm di KERNEL_OFFSET
; ================================================================

[BITS 16]
[ORG 0x7c00]

KERNEL_OFFSET equ 0x8000        ; kernel dimuat ke sini oleh disk_load

; ----------------------------------------------------------------
; start — titik masuk bootloader (Real Mode)
; Inisialisasi segment, stack, lalu muat dan jalankan kernel.
; ----------------------------------------------------------------
start:
    xor ax, ax
    mov ds, ax                  ; data segment = 0
    mov es, ax                  ; extra segment = 0
    mov ss, ax                  ; stack segment = 0
    mov sp, 0x7c00              ; stack tumbuh ke bawah dari 0x7C00

    mov si, msg
    call print_string

    mov [boot_drive], dl        ; BIOS simpan nomor drive di DL

    mov word [kernel_total_sectors], KERNEL_SECTORS
    mov dl, [boot_drive]
    call disk_load

    cli                         ; matikan interrupt sebelum masuk PM
    call switch_to_pm

; ----------------------------------------------------------------
; print_string — cetak string null-terminated via BIOS teletype
; Input : SI = pointer ke string
; Ubah  : AX, BH
; ----------------------------------------------------------------
print_string:
    mov ah, 0x0E                ; BIOS fn: teletype output
    mov bh, 0                   ; halaman video 0
.loop:
    lodsb                       ; AL = [SI], SI++
    cmp al, 0
    je  .done
    int 0x10
    jmp .loop
.done:
    ret

; ----------------------------------------------------------------
; disk_load — muat kernel dari disk ke memori 0x8000
;
; Menggunakan BIOS INT 13h/42h (LBA Extended Read) dengan batch
; maksimal 63 sektor agar kompatibel dengan semua BIOS.
; DI = akumulator sektor yang sudah dibaca.
; Alamat tujuan dihitung: segment = 0x0800 + DI*32
;   → flat address = segment×16 = 0x8000 + DI×512
; ----------------------------------------------------------------
disk_load:
    push di
    push si

    xor  di, di                 ; reset penghitung sektor

.batch:
    mov  ax, di
    mov  bx, word [kernel_total_sectors]
    cmp  ax, bx
    jae  .done                  ; semua sektor sudah dibaca

    sub  bx, ax                 ; bx = sisa sektor
    cmp  bx, 63
    jbe  .fit
    mov  bx, 63                 ; batasi 63 sektor per panggilan BIOS

.fit:
    ; Isi DAP: jumlah sektor
    mov  byte [dap+2], bl
    mov  byte [dap+3], 0

    ; Isi DAP: buffer tujuan
    push bx
    mov  ax, di
    shl  ax, 5                  ; ax = DI * 32
    add  ax, 0x0800             ; segment = 0x800 + DI*32
    mov  [dap+6], ax            ; DAP segment
    mov  word [dap+4], 0        ; DAP offset = 0

    ; Isi DAP: LBA (kernel mulai di LBA 1, LBA 0 = bootloader)
    mov  ax, di
    inc  ax
    mov  [dap+8],  ax           ; LBA [15:0]
    mov  word [dap+10], 0       ; LBA [31:16]
    pop  bx

    ; Jalankan Extended Read
    push bx
    mov  ah, 0x42
    mov  si, dap
    int  0x13
    pop  bx
    jc   disk_error             ; CF=1 = error baca disk

    xor  bh, bh
    add  di, bx                 ; maju penghitung sektor
    jmp  .batch

.done:
    pop si
    pop di
    ret

; ----------------------------------------------------------------
; DAP — Disk Address Packet untuk INT 13h/42h (16 byte)
; Diisi ulang oleh disk_load setiap batch.
; ----------------------------------------------------------------
dap:
    db 0x10, 0                  ; [0] ukuran=16, [1] reserved
    dw 0                        ; [2-3] jumlah sektor
    dw 0, 0                     ; [4-5] offset buffer, [6-7] segment buffer
    dd 0, 0                     ; [8-15] LBA 64-bit (hanya 32-bit bawah dipakai)

; ----------------------------------------------------------------
; disk_error — tampilkan pesan error lalu halt
; ----------------------------------------------------------------
disk_error:
    mov si, disk_err_msg
    call print_string
.halt:
    hlt
    jmp .halt

; ----------------------------------------------------------------
; Data
; ----------------------------------------------------------------
disk_err_msg:           db 'Disk read error!', 0x0D, 0x0A, 0
boot_drive:             db 0
kernel_total_sectors:   dw 0
msg:                    db 'Hello from MyOS!', 0x0D, 0x0A, 0

; ================================================================
; GDT — Global Descriptor Table (model flat 32-bit)
;   Selector 0x08 = code ring-0  (CODE_SEG)
;   Selector 0x10 = data ring-0  (DATA_SEG)
; ================================================================

gdt_start:

gdt_null:                   ; entry 0: null descriptor (wajib, selalu 0)
    dd 0x00000000
    dd 0x00000000

gdt_code:                   ; entry 1: code ring-0, base=0, limit=4GB
    dw 0xFFFF               ; limit [15:0]
    dw 0x0000               ; base  [15:0]
    db 0x00                 ; base  [23:16]
    db 10011010b            ; P=1 DPL=0 S=1 Type=exec/read
    db 11001111b            ; G=4KB D=32bit limit[19:16]=0xF
    db 0x00                 ; base  [31:24]

gdt_data:                   ; entry 2: data ring-0, base=0, limit=4GB
    dw 0xFFFF               ; limit [15:0]
    dw 0x0000               ; base  [15:0]
    db 0x00                 ; base  [23:16]
    db 10010010b            ; P=1 DPL=0 S=1 Type=data/write
    db 11001111b            ; G=4KB D=32bit limit[19:16]=0xF
    db 0x00                 ; base  [31:24]

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1     ; ukuran GDT dalam byte minus 1
    dd gdt_start                    ; alamat linear GDT

CODE_SEG equ gdt_code - gdt_start  ; = 0x08
DATA_SEG equ gdt_data - gdt_start  ; = 0x10

; ----------------------------------------------------------------
; switch_to_pm — aktifkan Protected Mode, lompat ke kode 32-bit
; ----------------------------------------------------------------
switch_to_pm:
    lgdt [gdt_descriptor]       ; muat GDT ke CPU

    mov eax, cr0
    or  eax, 1                  ; set bit PE (Protection Enable)
    mov cr0, eax

    jmp CODE_SEG:pm_entry       ; far jump: flush pipeline, masuk 32-bit

; ================================================================
; Kode 32-bit Protected Mode
; ================================================================
[BITS 32]

; ----------------------------------------------------------------
; pm_entry — titik masuk pertama setelah switch 32-bit
; Reload semua segment register dengan selector DATA_SEG, lalu
; siapkan stack kernel dan lompat ke kernel_entry.asm.
; ----------------------------------------------------------------
pm_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000            ; puncak stack kernel (di bawah 640KB)

    ; Diagnostik: tulis penanda 'PM' ke VGA text buffer 0xB8000
    mov word [0xB8000], 0x0F50  ; 'P' putih di atas hitam
    mov word [0xB8002], 0x0F4D  ; 'M'

    jmp KERNEL_OFFSET           ; serahkan kontrol ke kernel_entry.asm

.halt:
    hlt
    jmp .halt

; ----------------------------------------------------------------
; Boot signature — dibutuhkan BIOS untuk mengenali sektor ini
; ----------------------------------------------------------------
times 510 - ($ - $$) db 0
dw 0xAA55