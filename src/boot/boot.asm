[BITS 16]
[ORG 0x7c00]

start:
    xor ax, ax      ; ax = 0
    mov ds, ax      ; data segment =  0
    mov es, ax      ; extra segment = 0
    mov ss, ax      ; stack segment = 0
    mov sp, 0x7c00  ; stack pointer dimulai tepat sebelum bootloader
    mov si, msg     ; arahkan SI ke alamat teks yang mau dicetak
    call print_string
    ; setelah selesai print, loop selamanya(halt)
.halt:
    hlt
    jmp .halt
print_string:
    mov ah,0x0E     ; fungsi BIOS teletype output
    mov bh, 0       ; halaman video 0
.loop:
    lodsb           ; load byte dari [SI] ke AL, lalu SI++
    cmp al, 0       ; apakah ini karakter null (akhir string)?
    je .done        ; kalau ya, selesai
    int 0x10        ; panggil BIOS untuk print karakter di AL
    jmp .loop       ; ulangi untuk karakter berikutnya
.done:
    ret             ; kembali ke pemanggil (setelah 'call print_string')

msg:
    db 'Hello from MyOS!', 0x0D, 0x0A, 0

times 510 - ($ - $$) db 0
dw 0xAA55