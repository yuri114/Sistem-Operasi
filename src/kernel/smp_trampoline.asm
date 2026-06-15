; smp_trampoline.asm — AP startup trampoline (real-mode -> long mode)
;
; Flat binary yang di-copy ke alamat fisik 0x7000, dijalankan AP setelah SIPI.
; SIPI vector=7 → CS:IP = 0x0700:0x0000 → physical 0x7000.
;
; ORG 0x7000 wajib agar semua label punya nilai alamat fisik yang benar:
;   - Real mode  (DS=0): physical = 0 + label_value = label ✓
;   - 32-bit pmode (DS base=0): flat_addr = label ✓
;   - 64-bit long mode (flat): addr = label ✓
;
; Layout patch field (diisi smp.c sebelum SIPI):
;   Offset dari awal binary           Alamat fisik
;   0x10 : qword patch_pml4     →    0x7010  (PML4 phys addr, default 0x1000)
;   0x18 : qword patch_gdt_ptr  →    0x7018  (ptr ke struct kernel_gdt64_ptr)
;   0x20 : qword patch_ap_entry →    0x7020  (phys addr fungsi C smp_ap_entry)

BITS 16
ORG 0x7000

    jmp ap_start
    nop

; ------------------------------------------------------------------
; Patch fields — harus tetap di offset 0x10/0x18/0x20 dari awal file
; ($$=0x7000, jadi $-$$ = offset dari awal, bukan dari 0x7000)
; ------------------------------------------------------------------
TIMES (0x10 - ($-$$)) db 0
patch_pml4:      dq 0x0000000000001000
patch_gdt_ptr:   dq 0x0000000000000000
patch_ap_entry:  dq 0x0000000000000000

ap_start:
    cli

    ; Inisialisasi segment registers di real mode.
    ; DS wajib = 0 agar [label] => physical = 0 + label_value (ORG 0x7000) ✓
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Muat GDT 32-bit minimal untuk masuk Protected Mode
    o32 lgdt [gdt32_ptr]

    ; Set CR0.PE
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    ; Far jump aktifkan 32-bit code segment (CS=0x08 dari gdt32)
    jmp 0x08:pmode_entry

BITS 32
pmode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Enable PAE (diperlukan untuk 4-level paging)
    mov eax, cr4
    or  eax, 1 << 5
    mov cr4, eax

    ; Load CR3 = PML4 boot kernel (patched: default 0x1000)
    ; Dengan ORG 0x7000 + pmode flat DS, [patch_pml4] = [0x7010] ✓
    mov eax, dword [patch_pml4]
    mov cr3, eax

    ; Set EFER.LME (bit 8)
    mov ecx, 0xC0000080
    rdmsr
    or  eax, 1 << 8
    wrmsr

    ; Load kernel 64-bit GDT langsung dari struct GDTR BSP.
    ; patch_gdt_ptr = alamat kernel_gdt64_ptr (6-byte GDTR struct).
    ; lgdt [eax] di 32-bit pmode membaca 6-byte: limit(2) + base(4). ✓
    ; Tidak memakai gdtr_tmp — cara itu meninggalkan limit=0 (bug!).
    mov eax, dword [patch_gdt_ptr]   ; eax = alamat kernel_gdt64_ptr
    lgdt [eax]

    ; Enable paging → CPU masuk Long Mode
    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    ; Far jump ke 64-bit code segment (CS=0x08 sudah 64-bit di kernel GDT)
    jmp 0x08:lmode_entry

BITS 64
lmode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; Stack per-AP unik berdasarkan LAPIC ID (8KB per AP)
    ; Base 0x4200000: di atas .bss (0x4000000-0x41FFFFF), masih dalam region
    ; 2MB yang dipetakan pd_low[32] (vmm.c) sehingga tetap accessible walau
    ; CR3 user process aktif. (Base lama 0x9F000 overlap dengan tail .data
    ; setelah kernel bertambah besar -> korupsi global var -> triple fault.)
    ; Formula: 0x4200000 - apic_id * 8192
    ;   AP1 (id=1): 0x41FE000-0x4200000, AP2 (id=2): 0x41FC000-0x41FE000
    ;
    ; PENTING: jangan tulis [0xFEE00020] langsung — NASM di BITS 64 menghasilkan
    ; SIB+disp32 yang di-sign-extend ke 0xFFFFFFFFFEE00020 (tidak di-map!).
    ; Gunakan register 64-bit eksplisit agar address zero-extended dengan benar.
    mov rcx, 0x00000000FEE00020    ; zero-extended 64-bit address LAPIC_ID reg
    mov eax, dword [rcx]           ; baca LAPIC_ID (safe: address via register)
    shr eax, 24
    and eax, 0xFF
    movzx rax, al
    shl   rax, 13           ; * 8192 (8KB per AP)
    mov   rsp, 0x4200000
    sub   rsp, rax          ; AP1: 0x41FE000, AP2: 0x41FC000, ...

    ; Lompat ke fungsi C smp_ap_entry (64-bit RIP-relative dari patch field)
    ; [patch_ap_entry] = [0x7020] dengan ORG 0x7000 ✓
    mov  rax, [patch_ap_entry]
    call rax                ; call (bukan jmp) agar RSP sesuai ABI C

.hang:
    hlt
    jmp .hang

; ------------------------------------------------------------------
; GDT 32-bit minimal (hanya untuk transisi real-mode → protected-mode)
; Tidak dipakai lagi setelah lgdt [kernel_gdt64_ptr] di pmode.
; ------------------------------------------------------------------
ALIGN 8
gdt32:
    dq 0x0000000000000000           ; null
    dq 0x00CF9A000000FFFF           ; 0x08 code 32-bit DPL=0
    dq 0x00CF92000000FFFF           ; 0x10 data 32-bit DPL=0
gdt32_end:

gdt32_ptr:
    dw gdt32_end - gdt32 - 1
    dd gdt32

