; ================================================================
; kernel_entry.asm — Titik Masuk Kernel (32-bit Protected Mode)
;
; Dipanggil langsung oleh bootloader (jmp KERNEL_OFFSET = 0x8000).
; Tiga tugas sebelum kernel_main():
;   1. Zero BSS (termasuk page_directory & page_table)
;   2. Pasang kernel GDT sendiri (GDT bootloader bisa tertimpa BSS)
;   3. Reload CS via far jump, lalu panggil kernel_main()
; ================================================================

[BITS 32]
[EXTERN kernel_main]

extern __bss_start
extern __bss_end

; ----------------------------------------------------------------
; 1. Nol-kan seluruh BSS
; ----------------------------------------------------------------
    xor eax, eax
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    rep stosb

; ----------------------------------------------------------------
; 2. Pasang GDT kernel (GDT bootloader di ~0x7C78 bisa tertimpa
;    oleh zeroing BSS di atas — gunakan GDT yang ada di .text ini)
; ----------------------------------------------------------------
    lgdt [kernel_gdt_ptr]

; ----------------------------------------------------------------
; 3. Far jump untuk flush prefetch queue dan reload CS
; ----------------------------------------------------------------
    jmp 0x08:flush_cs

; ----------------------------------------------------------------
; flush_cs — reload semua segment registers ke DATA_SEG kernel,
;            lalu panggil kernel_main()
; ----------------------------------------------------------------
flush_cs:
    mov ax, 0x10            ; data segment selector (0x10)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Diagnostik: tulis penanda 'EL' ke VGA text buffer
    mov word [0xB8004], 0x0F45  ; 'E' putih di atas hitam
    mov word [0xB8006], 0x0F4C  ; 'L'

    call kernel_main

    ; Seharusnya tidak pernah dicapai
    jmp $

; ================================================================
; GDT Kernel — Disimpan di .text agar selalu dapat diakses
;
;   0x00  null        — wajib
;   0x08  code ring-0 — selector CS kernel
;   0x10  data ring-0 — selector DS/ES/FS/GS/SS kernel
;   0x18  code ring-3 — selector CS user  (pakai 0x1B = 0x18|3)
;   0x20  data ring-3 — selector DS user  (pakai 0x23 = 0x20|3)
;   0x28  TSS         — diisi oleh tss_init() dari C
; ================================================================
kernel_gdt:

    ; entry 0: null (wajib)
    dq 0

    ; entry 1: code ring-0  (selector 0x08)
    dw 0xFFFF               ; limit [15:0]
    dw 0x0000               ; base  [15:0]
    db 0x00                 ; base  [23:16]
    db 10011010b            ; P=1 DPL=0 S=1 Type=exec/read
    db 11001111b            ; G=4KB D=32bit limit[19:16]=0xF
    db 0x00                 ; base  [31:24]

    ; entry 2: data ring-0  (selector 0x10)
    dw 0xFFFF               ; limit [15:0]
    dw 0x0000               ; base  [15:0]
    db 0x00                 ; base  [23:16]
    db 10010010b            ; P=1 DPL=0 S=1 Type=data/write
    db 11001111b            ; G=4KB D=32bit limit[19:16]=0xF
    db 0x00                 ; base  [31:24]

    ; entry 3: code ring-3  (selector 0x1B = 0x18|3)
user_code_desc:
    dw 0xFFFF               ; limit [15:0]
    dw 0x0000               ; base  [15:0]
    db 0x00                 ; base  [23:16]
    db 11111010b            ; P=1 DPL=3 S=1 Type=exec/read
    db 11001111b            ; G=4KB D=32bit limit[19:16]=0xF
    db 0x00                 ; base  [31:24]

    ; entry 4: data ring-3  (selector 0x23 = 0x20|3)
user_data_desc:
    dw 0xFFFF               ; limit [15:0]
    dw 0x0000               ; base  [15:0]
    db 0x00                 ; base  [23:16]
    db 11110010b            ; P=1 DPL=3 S=1 Type=data/write
    db 11001111b            ; G=4KB D=32bit limit[19:16]=0xF
    db 0x00                 ; base  [31:24]

    ; entry 5: TSS  (selector 0x28) — 8 byte diisi oleh tss_init()
tss_desc:
    dq 0
    global tss_desc

kernel_gdt_end:

; ----------------------------------------------------------------
; GDT pointer — dimuat oleh lgdt di atas
; ----------------------------------------------------------------
kernel_gdt_ptr:
    dw kernel_gdt_end - kernel_gdt - 1     ; ukuran GDT dalam byte minus 1
    dd kernel_gdt                           ; alamat linear kernel_gdt