; ==================================================================
; kernel_entry.asm — Titik Masuk Kernel
;
; Alur (bootloader menempatkan kita di 32-bit Protected Mode, 0x8000):
;   [BITS 32]
;   1. Zero BSS
;   2. Siapkan 4-level page tables di 0x1000-0x6FFF (identity map 4GB)
;   3. Aktifkan PAE, load CR3=0x1000, set EFER.LME, load GDT64, enable PG
;   4. Far jump ke [BITS 64] long_mode_entry
;   [BITS 64]
;   5. Reload segment registers, set RSP
;   6. Panggil kernel_main()
; ==================================================================

[BITS 32]

extern kernel_main
extern __bss_start
extern __bss_end

; ------------------------------------------------------------------
; 1. Nol-kan seluruh BSS
; ------------------------------------------------------------------
    xor  eax, eax
    mov  edi, __bss_start
    mov  ecx, __bss_end
    sub  ecx, edi
    rep  stosb

; ------------------------------------------------------------------
; 2. Siapkan 4-level page tables di 0x1000-0x6FFF
;    0x1000: PML4 — 512 x 8 byte
;    0x2000: PDPT — 512 x 8 byte
;    0x3000: PD[0-1GB]  — 2MB pages, User-accessible (covers 0x300000 user code)
;    0x4000: PD[1-2GB]  — 2MB pages, kernel-only
;    0x5000: PD[2-3GB]
;    0x6000: PD[3-4GB]  — mencakup VBE LFB 0xE0000000
; ------------------------------------------------------------------
    ; Zero 6 halaman (24KB)
    xor  eax, eax
    mov  edi, 0x1000
    mov  ecx, (0x6000 / 4)
    rep  stosd

    ; PML4[0] = PDPT di 0x2000, P+RW
    mov  dword [0x1000], 0x2003
    mov  dword [0x1004], 0

    ; PDPT[0..3] = masing-masing PD, P+RW
    mov  dword [0x2000], 0x3003
    mov  dword [0x2004], 0
    mov  dword [0x2008], 0x4003
    mov  dword [0x200C], 0
    mov  dword [0x2010], 0x5003
    mov  dword [0x2014], 0
    mov  dword [0x2018], 0x6003
    mov  dword [0x201C], 0

    ; PD[0-1GB]: entri-i = i*2MB | 0x87 (P+RW+User+PS) — User agar user code di 0x300000 ok
    mov  edi, 0x3000
    xor  ecx, ecx
.fill_pd0:
    mov  eax, ecx
    shl  eax, 21
    or   eax, 0x87
    mov  [edi + ecx*8], eax
    mov  dword [edi + ecx*8 + 4], 0
    inc  ecx
    cmp  ecx, 512
    jl   .fill_pd0

    ; PD[1-2GB]: base=0x40000000, kernel-only (0x83)
    mov  edi, 0x4000
    xor  ecx, ecx
.fill_pd1:
    mov  eax, ecx
    shl  eax, 21
    add  eax, 0x40000000
    or   eax, 0x83
    mov  [edi + ecx*8], eax
    mov  dword [edi + ecx*8 + 4], 0
    inc  ecx
    cmp  ecx, 512
    jl   .fill_pd1

    ; PD[2-3GB]: base=0x80000000
    mov  edi, 0x5000
    xor  ecx, ecx
.fill_pd2:
    mov  eax, ecx
    shl  eax, 21
    add  eax, 0x80000000
    or   eax, 0x83
    mov  [edi + ecx*8], eax
    mov  dword [edi + ecx*8 + 4], 0
    inc  ecx
    cmp  ecx, 512
    jl   .fill_pd2

    ; PD[3-4GB]: base=0xC0000000, kernel-only (VBE LFB ada di sini)
    mov  edi, 0x6000
    xor  ecx, ecx
.fill_pd3:
    mov  eax, ecx
    shl  eax, 21
    add  eax, 0xC0000000
    or   eax, 0x83
    mov  [edi + ecx*8], eax
    mov  dword [edi + ecx*8 + 4], 0
    inc  ecx
    cmp  ecx, 512
    jl   .fill_pd3

; ------------------------------------------------------------------
; 3. Aktifkan PAE (CR4 bit 5)
; ------------------------------------------------------------------
    mov  eax, cr4
    or   eax, 1 << 5
    mov  cr4, eax

; ------------------------------------------------------------------
; 4. Load PML4 ke CR3
; ------------------------------------------------------------------
    mov  eax, 0x1000
    mov  cr3, eax

; ------------------------------------------------------------------
; 5. Set EFER.LME=1 via WRMSR (MSR 0xC0000080)
; ------------------------------------------------------------------
    mov  ecx, 0xC0000080
    rdmsr
    or   eax, 1 << 8
    wrmsr

; ------------------------------------------------------------------
; 6. Load GDT 64-bit sebelum enable paging
; ------------------------------------------------------------------
    lgdt [kernel_gdt64_ptr]

; ------------------------------------------------------------------
; 7. Enable Paging (CR0.PG=1) -- CPU masuk Long Mode (EFER.LME sudah set)
; ------------------------------------------------------------------
    mov  eax, cr0
    or   eax, 0x80000000
    mov  cr0, eax

; ------------------------------------------------------------------
; 8. Far jump ke 64-bit segment (selector 0x08 = kernel code 64-bit)
; ------------------------------------------------------------------
    jmp  0x08:long_mode_entry

; ==================================================================
; Kode 64-bit Long Mode
; ==================================================================
[BITS 64]

long_mode_entry:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; Stack kernel awal di bawah 640KB (aman sebelum heap diinisialisasi)
    mov  rsp, 0x90000

    ; Tulis 'LM' ke VGA text buffer (diagnostik)
    mov  word [0xB8000], 0x0F4C
    mov  word [0xB8002], 0x0F4D

    call kernel_main

.halt:
    cli
    hlt
    jmp  .halt

; ==================================================================
; GDT 64-bit
;   0x00  null
;   0x08  kernel code ring-0  (L=1, D=0, P=1, DPL=0)
;   0x10  kernel data ring-0  (L=0, D=1, P=1, DPL=0)
;   0x18  user code  ring-3   (L=1, D=0, P=1, DPL=3) => selector 0x1B
;   0x20  user data  ring-3   (L=0, D=1, P=1, DPL=3) => selector 0x23
;   0x28  TSS64 low  (16 byte, 2 entri GDT) -- diisi tss64_init()
;   0x30  TSS64 high
; ==================================================================
align 8
kernel_gdt64:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x00AFFA000000FFFF
    dq 0x00CFF2000000FFFF
global tss64_desc
tss64_desc:
    dq 0
    dq 0
kernel_gdt64_end:

kernel_gdt64_ptr:
    dw kernel_gdt64_end - kernel_gdt64 - 1
    dq kernel_gdt64
