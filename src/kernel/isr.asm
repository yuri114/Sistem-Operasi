[BITS 32]

; Export fungsi ke C
global idt_load
global irq0
global irq1

; Import handler dari keyboard.c
extern keyboard_handler
extern timer_handler
extern mouse_handler

; ----------------------------------------------------
; idt_load - dipanggil dari idt.c
; Menerima pointer ke idt_descriptor_t sebagai argumen
; ----------------------------------------------------
idt_load:
    mov eax, [esp + 4]  ; ambil argumen pertama (pointer ke idt_desc)
    lidt [eax]          ; load IDT ke CPU
    ret

; ----------------------------------------------------
; IRQ0 - Timer (interrupt 32)
; Tidak perlu lakukan apa-apa selain kirim EOI
; ----------------------------------------------------
irq0:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call timer_handler

    extern task_switch
    extern current_esp
    extern next_esp
    call task_switch

    ; simpan ESP task sekarang (skip jika 0 = bootstrap task 0)
    mov eax, [current_esp]
    test eax, eax
    jz .skip_save
    mov [eax], esp          ; simpan ESP task lama

.skip_save:
    ; load ESP task berikutnya
    mov eax, [next_esp]
    test eax, eax
    jz .no_switch
    mov esp, [eax]          ; ganti stack ke task baru

.no_switch:

    mov al, 0x20
    out 0x20, al        ; kirim EOI ke master PIC
    
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    iret

; ----------------------------------------------------
; IRQ1 - Keyboard (interrupt 33)
; Atur segment, panggil handler C, kirim EOI
; ----------------------------------------------------
irq1:
    pusha

    ; simpan dan ganti data segment ke kernel segment
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call keyboard_handler   ; panggil handler keyboard di C

    mov al, 0x20
    out 0x20, al            ; kirim EOI ke master PIC

    ; restore data segment
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    iret

; ----------------------------------------------------
; IRQ12 - PS/2 Mouse (interrupt 44 = slave IRQ4)
; Kirim EOI ke slave (0xA0) DAN master (0x20)
; ----------------------------------------------------
global irq12
irq12:
    pusha
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call mouse_handler

    mov al, 0x20
    out 0xA0, al        ; EOI ke slave PIC
    out 0x20, al        ; EOI ke master PIC

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    iret

extern syscall_handler
global int80_handler

int80_handler:
    pusha
    push edx                ; argumen ke-3 (tidak dipakai sekarang)
    push ebx                ; argumen ke-2 (pointer string dll)
    push eax                ; argumen ke-1 (nomor syscall)
    call syscall_handler
    add esp, 12             ; bersihkan 3 argumen dari stack
    mov [esp+28], eax
    popa
    iret

; ============================================================
; Exception handlers INT 0-14 — CPU exceptions
; ============================================================
extern exception_handler

; Exception TANPA error code (CPU tidak push error code)
%macro EXC_NOERR 1
global exc%1
exc%1:
    push dword 0        ; fake error code
    push dword %1       ; nomor exception
    jmp exc_common
%endmacro

; Exception DENGAN error code (CPU sudah push error code sebelum kita dipanggil)
%macro EXC_ERR 1
global exc%1
exc%1:
    push dword %1       ; nomor exception (error code sudah ada di stack dari CPU)
    jmp exc_common
%endmacro

EXC_NOERR 0    ; #DE Divide Error
EXC_NOERR 1    ; #DB Debug
EXC_NOERR 2    ;     NMI
EXC_NOERR 3    ; #BP Breakpoint
EXC_NOERR 4    ; #OF Overflow
EXC_NOERR 5    ; #BR Bound Range
EXC_NOERR 6    ; #UD Invalid Opcode
EXC_NOERR 7    ; #NM Device Not Available
EXC_ERR   8    ; #DF Double Fault     (error code selalu 0)
EXC_NOERR 9    ;     Coprocessor Segment Overrun
EXC_ERR   10   ; #TS Invalid TSS
EXC_ERR   11   ; #NP Segment Not Present
EXC_ERR   12   ; #SS Stack Fault
EXC_ERR   13   ; #GP General Protection Fault
EXC_ERR   14   ; #PF Page Fault

exc_common:
    ; Posisi stack saat masuk (dari ESP[0] ke alamat lebih tinggi):
    ;   [ESP+ 0] = exc_num         (kita push)
    ;   [ESP+ 4] = error_code      (kita push 0, atau error code dari CPU)
    ;   [ESP+ 8] = EIP             (CPU push, alamat instruksi yang salah)
    ;   [ESP+12] = CS
    ;   [ESP+16] = EFLAGS
    ;   [ESP+20] = ESP_user, [ESP+24] = SS_user  (hanya jika ada perubahan privilege)
    pusha               ; push 8 register = 32 bytes
    mov ax, ds
    push eax            ; simpan DS = 4 bytes
    ; Sekarang: exc_num=[ESP+36], error_code=[ESP+40], EIP=[ESP+44]

    mov ax, 0x10        ; kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov eax, [esp + 36]   ; exc_num
    mov ebx, [esp + 40]   ; error_code
    mov ecx, [esp + 44]   ; EIP (alamat instruksi penyebab)
    mov edx, cr2          ; CR2 = alamat akses yang menyebabkan #PF

    push edx              ; arg4: cr2
    push ecx              ; arg3: eip
    push ebx              ; arg2: error_code
    push eax              ; arg1: exc_num
    call exception_handler   ; tidak pernah kembali (cli+hlt di dalam)
    add esp, 16

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8          ; buang exc_num dan error_code
    iret