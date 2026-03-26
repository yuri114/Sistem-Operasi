; ==================================================================
; isr.asm — Interrupt Service Routines (64-bit Long Mode)
;
; Konvensi push/pop register (15 GPR, 8 byte tiap):
;   push rax, rbx, rcx, rdx, rsi, rdi, rbp, r8..r15
; Offset dari RSP setelah semua 15 push:
;   [rsp+ 0]=r15, [rsp+ 8]=r14, [rsp+16]=r13, [rsp+24]=r12,
;   [rsp+32]=r11, [rsp+40]=r10, [rsp+48]=r9,  [rsp+56]=r8,
;   [rsp+64]=rbp, [rsp+72]=rdi, [rsp+80]=rsi, [rsp+88]=rdx,
;   [rsp+96]=rcx, [rsp+104]=rbx, [rsp+112]=rax
; ==================================================================

[BITS 64]

global idt_load
global irq0
global irq1
global irq12
global int80_handler

extern keyboard_handler
extern timer_handler
extern mouse_handler
extern task_switch
extern current_rsp
extern next_rsp
extern syscall_handler

; ------------------------------------------------------------------
; Macro simpan/pulihkan semua GPR
; ------------------------------------------------------------------
%macro SAVE_REGS 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro RESTORE_REGS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

; ------------------------------------------------------------------
; idt_load — muat IDT (dipanggil dari idt.c)
; Argumen (SysV AMD64): rdi = pointer ke idt_descriptor
; ------------------------------------------------------------------
idt_load:
    lidt [rdi]
    ret

; ------------------------------------------------------------------
; IRQ0 — Timer (interrupt 32)
; Tangani timer tick lalu lakukan task switch jika perlu.
; ------------------------------------------------------------------
irq0:
    SAVE_REGS

    call timer_handler

    call task_switch            ; mungkin set current_rsp / next_rsp

    ; Simpan RSP task sekarang
    mov  rax, [current_rsp]     ; rax = &tasks[current].rsp (atau 0)
    test rax, rax
    jz   .skip_save
    mov  [rax], rsp             ; tasks[current].rsp = RSP kita sekarang
.skip_save:

    ; Muat RSP task berikutnya
    mov  rax, [next_rsp]        ; rax = &tasks[next].rsp (atau 0)
    test rax, rax
    jz   .no_switch
    mov  rsp, [rax]             ; RSP = tasks[next].rsp
.no_switch:

    mov  al, 0x20
    out  0x20, al               ; EOI ke master PIC

    RESTORE_REGS
    iretq

; ------------------------------------------------------------------
; IRQ1 — Keyboard (interrupt 33)
; ------------------------------------------------------------------
irq1:
    SAVE_REGS
    call keyboard_handler
    mov  al, 0x20
    out  0x20, al
    RESTORE_REGS
    iretq

; ------------------------------------------------------------------
; IRQ12 — PS/2 Mouse (interrupt 44 = slave IRQ4)
; ------------------------------------------------------------------
irq12:
    SAVE_REGS
    call mouse_handler
    mov  al, 0x20
    out  0xA0, al               ; EOI slave PIC
    out  0x20, al               ; EOI master PIC
    RESTORE_REGS
    iretq

; ------------------------------------------------------------------
; INT 0x80 — Syscall (ring 3 memanggil kernel)
;
; Konvensi (user space):
;   rax = nomor syscall
;   rbx = argumen ke-1
;   rdx = argumen ke-2
;
; Konvensi SysV AMD64 untuk syscall_handler(rax, rbx, rdx):
;   rdi = arg1, rsi = arg2, rdx = arg3
; ------------------------------------------------------------------
int80_handler:
    SAVE_REGS

    ; Ambil rax/rbx/rdx asli dari stack (offset setelah 15 push):
    ;   rax ada di [rsp+112], rbx di [rsp+104], rdx di [rsp+88]
    mov  rdi, [rsp + 112]       ; nomor syscall  -> rdi
    mov  rsi, [rsp + 104]       ; arg1 (ptr/val) -> rsi
    mov  rdx, [rsp + 88]        ; arg2 (ptr/val) -> rdx (sudah di rdx, tapi aman)

    call syscall_handler        ; return value di rax

    ; Simpan return value ke slot rax di stack agar terpulihkan saat pop rax
    mov  [rsp + 112], rax

    mov  al, 0x20
    out  0x20, al               ; EOI (int 0x80 pakai interrupt gate, tetap butuh EOI)

    RESTORE_REGS
    iretq

; ==================================================================
; Exception Handlers INT 0-14
; ==================================================================
extern exception_handler

%macro EXC_NOERR 1
global exc%1
exc%1:
    push qword 0            ; fake error code
    push qword %1           ; nomor exception
    jmp  exc_common
%endmacro

%macro EXC_ERR 1
global exc%1
exc%1:
    push qword %1           ; nomor exception (error code sudah ada dari CPU)
    jmp  exc_common
%endmacro

EXC_NOERR 0
EXC_NOERR 1
EXC_NOERR 2
EXC_NOERR 3
EXC_NOERR 4
EXC_NOERR 5
EXC_NOERR 6
EXC_NOERR 7
EXC_ERR   8
EXC_NOERR 9
EXC_ERR   10
EXC_ERR   11
EXC_ERR   12
EXC_ERR   13
EXC_ERR   14

exc_common:
    ; Stack saat masuk:
    ;   [rsp+ 0] = exc_num    (kita push)
    ;   [rsp+ 8] = error_code (kita push 0, atau dari CPU)
    ;   [rsp+16] = RIP   (CPU push)
    ;   [rsp+24] = CS
    ;   [rsp+32] = RFLAGS
    ;   [rsp+40] = RSP_user
    ;   [rsp+48] = SS_user
    SAVE_REGS
    ; Setelah 15 push (120 byte), offset +120:
    ;   exc_num    = [rsp+120]
    ;   error_code = [rsp+128]
    ;   RIP        = [rsp+136]

    mov  rdi, [rsp + 120]       ; exc_num
    mov  rsi, [rsp + 128]       ; error_code
    mov  rdx, [rsp + 136]       ; RIP
    mov  rcx, cr2               ; CR2 untuk page fault

    call exception_handler      ; tidak pernah kembali

    RESTORE_REGS
    add  rsp, 16                ; buang exc_num + error_code
    iretq
