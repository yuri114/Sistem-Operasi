; ================================================================
; boot.asm — Oria OS Stage-1 Bootloader (512 byte MBR)
;
; Alur kerja:
;   1. Inisialisasi segment & stack di Real Mode
;   2. Masuk "Unreal Mode" (DS/ES diberi flat descriptor limit 4GB)
;      agar bisa menulis ke alamat >1MB sambil tetap di real mode
;      (BIOS INT 13h/INT 10h tetap berfungsi normal).
;   3. Muat kernel dari disk secara ber-chunk:
;        - tiap chunk (maks 608KB) dibaca via BIOS INT 13h/42h ke
;          buffer sementara 0x8000-0x9FFFF (di bawah VGA hole),
;        - lalu di-copy (rep movsb 32-bit, unreal mode) ke alamat
;          akhirnya di KERNEL_LINK_BASE (>=1MB, tidak terbatas
;          oleh hole VGA 0xA0000-0xBFFFF).
;      Dengan ini ukuran kernel TIDAK dibatasi oleh 608KB lagi —
;      cukup tambah chunk berikutnya.
;   4. Aktifkan Protected Mode permanen, lompat ke kernel_entry.asm
;      di KERNEL_LINK_BASE.
; ================================================================

[BITS 16]
[ORG 0x7c00]

KERNEL_LINK_BASE equ 0x700000   ; alamat akhir kernel (lihat linker.ld: . = 0x700000)
STAGING_BASE     equ 0x8000     ; buffer sementara real-mode, di bawah VGA hole 0xA0000
CHUNK_SECTORS    equ 1216       ; (0xA0000-0x8000)/512 = kapasitas staging per chunk

; ----------------------------------------------------------------
; start — titik masuk bootloader (Real Mode)
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

    ; --------------------------------------------------------
    ; CATATAN: DS dibiarkan = 0 (sudah flat, base=0, aman dari
    ; BIOS yang reload DS internal). "Unreal mode" untuk ES
    ; (flat 4GB, dipakai copy_chunk_to_dest) di-setup ULANG
    ; tiap sebelum dipakai (lihat copy_chunk_to_dest) karena
    ; INT 13h BIOS bisa push/pop ES dan me-reset hidden base
    ; ES kembali ke ES_value*16 saat reload.
    ; --------------------------------------------------------

    ; --------------------------------------------------------
    ; Muat kernel ber-chunk: baca <= CHUNK_SECTORS sektor ke
    ; staging (0x8000), lalu relokasi ke KERNEL_LINK_BASE+offset.
    ; --------------------------------------------------------
    mov  word [kernel_total_sectors], KERNEL_SECTORS
    mov  word [sectors_done], 0

.load_loop:
    mov  ax, [kernel_total_sectors]
    mov  bx, [sectors_done]
    cmp  bx, ax
    jae  .load_done

    sub  ax, bx                 ; ax = sisa sektor
    cmp  ax, CHUNK_SECTORS
    jbe  .chunk_ok
    mov  ax, CHUNK_SECTORS
.chunk_ok:
    mov  [chunk_sectors], ax

    mov  dl, [boot_drive]
    call disk_load_chunk        ; baca [chunk_sectors] sektor ke STAGING_BASE
    call copy_chunk_to_dest      ; relokasi ke KERNEL_LINK_BASE + sectors_done*512

    mov  ax, [sectors_done]
    add  ax, [chunk_sectors]
    mov  [sectors_done], ax
    jmp  .load_loop

.load_done:
    cli                         ; matikan interrupt sebelum masuk PM permanen
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
; disk_load_chunk — muat [chunk_sectors] sektor ke STAGING_BASE
;
; Menggunakan BIOS INT 13h/42h (LBA Extended Read) dengan batch
; maksimal 63 sektor agar kompatibel dengan semua BIOS.
; DI = penghitung sektor LOKAL (0..chunk_sectors-1), staging
; selalu mulai dari STAGING_BASE (0x8000) untuk setiap chunk.
; LBA global = 1 + sectors_done + DI (LBA 0 = bootloader/MBR).
; Alamat tujuan: segment = 0x0800 + DI*32 -> flat = 0x8000 + DI*512
; ----------------------------------------------------------------
disk_load_chunk:
    push di
    push si
    push ax
    push bx

    xor  di, di                 ; reset penghitung sektor lokal

.batch:
    mov  ax, di
    mov  bx, [chunk_sectors]
    cmp  ax, bx
    jae  .done                  ; chunk ini sudah selesai

    sub  bx, ax                 ; bx = sisa sektor dalam chunk
    cmp  bx, 63
    jbe  .fit
    mov  bx, 63                 ; batasi 63 sektor per panggilan BIOS

.fit:
    ; Isi DAP: jumlah sektor
    mov  byte [dap+2], bl
    mov  byte [dap+3], 0

    ; Isi DAP: buffer tujuan (staging, selalu < 1MB)
    push bx
    mov  ax, di
    shl  ax, 5                  ; ax = DI * 32
    add  ax, 0x0800             ; segment = 0x800 + DI*32
    mov  [dap+6], ax            ; DAP segment
    mov  word [dap+4], 0        ; DAP offset = 0

    ; Isi DAP: LBA = 1 + sectors_done + DI
    mov  ax, [sectors_done]
    add  ax, di
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
    pop bx
    pop ax
    pop si
    pop di
    ret

; ----------------------------------------------------------------
; copy_chunk_to_dest — relokasi [chunk_sectors] sektor dari
; STAGING_BASE (0x8000, <1MB) ke KERNEL_LINK_BASE + sectors_done*512
; (bisa >1MB). DS=0 (flat sejak awal, aman). ES di-set ULANG ke
; flat 4GB (unreal mode) di sini karena INT 13h BIOS sebelumnya
; bisa saja reload ES (hidden base ES jadi ES_value*16, bukan 0).
; rep movsb 32-bit: sumber DS:ESI, tujuan ES:EDI.
; ----------------------------------------------------------------
copy_chunk_to_dest:
    push eax
    push ecx
    push esi
    push edi
    push bx

    ; Setup ES flat (base=0, limit=4GB) — lihat catatan di atas
    lgdt [gdt_descriptor]
    mov  eax, cr0
    or   al, 1
    mov  cr0, eax            ; PE=1 sebentar
    mov  bx, DATA_SEG
    mov  es, bx
    and  al, 0xFE
    mov  cr0, eax            ; PE=0, ES tetap flat

    pop  bx

    ; EDI = KERNEL_LINK_BASE + sectors_done*512
    xor  eax, eax
    mov  ax, [sectors_done]
    shl  eax, 9                 ; * 512
    add  eax, KERNEL_LINK_BASE
    mov  edi, eax

    ; ESI = STAGING_BASE (flat, selalu < 1MB)
    mov  esi, STAGING_BASE

    ; ECX = chunk_sectors * 512 (byte count)
    xor  eax, eax
    mov  ax, [chunk_sectors]
    shl  eax, 9
    mov  ecx, eax

    cld
    a32 rep movsb

    pop edi
    pop esi
    pop ecx
    pop eax
    ret

; ----------------------------------------------------------------
; DAP — Disk Address Packet untuk INT 13h/42h (16 byte)
; Diisi ulang oleh disk_load_chunk setiap batch.
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
sectors_done:           dw 0
chunk_sectors:          dw 0
msg:                    db 'Oria OS booting...', 0x0D, 0x0A, 0

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
; switch_to_pm — aktifkan Protected Mode permanen, lompat ke kode 32-bit
; ----------------------------------------------------------------
switch_to_pm:
    lgdt [gdt_descriptor]       ; muat GDT ke CPU (idempotent, sudah dimuat di atas)

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
    mov esp, 0x90000            ; stack sementara (tidak dipakai, dead value)

    ; Diagnostik: tulis penanda 'PM' ke VGA text buffer 0xB8000
    mov word [0xB8000], 0x0F50  ; 'P' putih di atas hitam
    mov word [0xB8002], 0x0F4D  ; 'M'

    jmp KERNEL_LINK_BASE         ; serahkan kontrol ke kernel_entry.asm

.halt:
    hlt
    jmp .halt

; ----------------------------------------------------------------
; Boot signature — dibutuhkan BIOS untuk mengenali sektor ini
; ----------------------------------------------------------------
times 510 - ($ - $$) db 0
dw 0xAA55
