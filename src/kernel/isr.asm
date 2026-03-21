[BITS 32]

; Export fungsi ke C
global idt_load
global irq0
global irq1

; Import handler dari keyboard.c
extern keyboard_handler
extern timer_handler

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

    ;di dalam irq0, setelah call timer_handler dan sebelum pop eax
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