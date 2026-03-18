[BITS 32]

; Export fungsi ke C
global idt_load
global irq0
global irq1

; Import handler dari keyboard.c
extern keyboard_handler

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
    mov al, 0x20
    out 0x20, al        ; kirim EOI ke master PIC
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