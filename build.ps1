# ============================================
# build.ps1 - Build Script untuk Oria OS
# Jalankan: .\build.ps1 [build|run|clean]
# ============================================

param(
    [string]$Action = "build"
)

# --- Path ke tools ---
$NASM    = "$env:LOCALAPPDATA\bin\NASM\nasm.exe"
$GCC     = "C:\msys64\mingw32\bin\i686-w64-mingw32-gcc.exe"
$LD      = "C:\msys64\mingw32\bin\i686-w64-mingw32-ld.exe"
$OBJCOPY = "C:\msys64\mingw32\bin\i686-w64-mingw32-objcopy.exe"
$QEMU    = "C:\Program Files\qemu\qemu-system-x86_64.exe"

# --- Direktori ---
$ROOT    = $PSScriptRoot
$SRC     = "$ROOT\src"
$BUILD   = "$ROOT\build"

# --- Pastikan folder build ada ---
New-Item -ItemType Directory -Force -Path $BUILD | Out-Null

function Build-Bootloader {
    $kernel_file = "$BUILD\kernel.bin"
    $input_file  = "$SRC\boot\boot.asm"
    $output_file = "$BUILD\boot.bin"

    if (-not (Test-Path $input_file)) {
        Write-Host "[ERROR] File tidak ditemukan: $input_file" -ForegroundColor Red
        exit 1
    }

    # hitung jumlah sektor kernel secara otomatis (+1 untuk pembulatan ke atas)
    $kernel_size    = (Get-Item $kernel_file).Length
    $kernel_sectors = [math]::Ceiling($kernel_size / 512)

    Write-Host "[NASM] Compiling bootloader (kernel=$kernel_size bytes, $kernel_sectors sektor)..." -ForegroundColor Cyan
    & $NASM -f bin $input_file -o $output_file -D KERNEL_SECTORS=$kernel_sectors

    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Bootloader compile gagal!" -ForegroundColor Red
        exit 1
    }
    Write-Host "       --> $output_file" -ForegroundColor Green
}

function Build-Kernel {
    Write-Host "[WSL]  Building kernel (nasm + gcc + ld + objcopy)..." -ForegroundColor Cyan
    $wslScript = @"
set -e
cd '/mnt/d/Sistem Operasi'
nasm -f elf64 src/kernel/kernel_entry.asm -o build/kernel_entry.o
nasm -f elf64 src/kernel/isr.asm         -o build/isr.o
nasm -f bin   src/kernel/smp_trampoline.asm -o build/smp_trampoline.bin
xxd -i build/smp_trampoline.bin > src/kernel/smp_trampoline_bin_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/hello.c \
    -o build/hello.elf
xxd -i build/hello.elf > src/kernel/hello_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/sender.c \
    -o build/sender.elf
xxd -i build/sender.elf > src/kernel/sender_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/writer.c \
    -o build/writer.elf
xxd -i build/writer.elf > src/kernel/writer_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/piper.c \
    -o build/piper.elf
xxd -i build/piper.elf > src/kernel/piper_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/pipe_sender.c \
    -o build/pipe_sender.elf
xxd -i build/pipe_sender.elf > src/kernel/pipe_sender_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/pipe_receiver.c \
    -o build/pipe_receiver.elf
xxd -i build/pipe_receiver.elf > src/kernel/pipe_receiver_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/devtest.c \
    -o build/devtest.elf
xxd -i build/devtest.elf > src/kernel/devtest_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/gfxtest.c \
    -o build/gfxtest.elf
xxd -i build/gfxtest.elf > src/kernel/gfxtest_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/gui_demo.c \
    -o build/gui_demo.elf
xxd -i build/gui_demo.elf > src/kernel/gui_demo_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/gui_term.c \
    -o build/gui_term.elf
xxd -i build/gui_term.elf > src/kernel/gui_term_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/paint.c \
    -o build/paint.elf
xxd -i build/paint.elf > src/kernel/paint_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/calc.c \
    -o build/calc.elf
xxd -i build/calc.elf > src/kernel/calc_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/notepad.c \
    -o build/notepad.elf
xxd -i build/notepad.elf > src/kernel/notepad_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/filemanager.c \
    -o build/filemanager.elf
xxd -i build/filemanager.elf > src/kernel/filemanager_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/clock.c \
    -o build/clock.elf
xxd -i build/clock.elf > src/kernel/clock_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/sysinfo.c \
    -o build/sysinfo.elf
xxd -i build/sysinfo.elf > src/kernel/sysinfo_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/threadtest.c \
    -o build/threadtest.elf
xxd -i build/threadtest.elf > src/kernel/threadtest_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/condtest.c \
    -o build/condtest.elf
xxd -i build/condtest.elf > src/kernel/condtest_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/forktest.c \
    -o build/forktest.elf
xxd -i build/forktest.elf > src/kernel/forktest_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/pipetest.c \
    -o build/pipetest.elf
xxd -i build/pipetest.elf > src/kernel/pipetest_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/mfs4test.c \
    -o build/mfs4test.elf
xxd -i build/mfs4test.elf > src/kernel/mfs4test_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/grep.c \
    -o build/grep.elf
xxd -i build/grep.elf > src/kernel/grep_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/ls.c \
    -o build/ls.elf
xxd -i build/ls.elf > src/kernel/ls_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/sigtest.c \
    -o build/sigtest.elf
xxd -i build/sigtest.elf > src/kernel/sigtest_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/futextest.c \
    -o build/futextest.elf
xxd -i build/futextest.elf > src/kernel/futextest_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/polltest.c \
    -o build/polltest.elf
xxd -i build/polltest.elf > src/kernel/polltest_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/cat.c \
    -o build/cat.elf
xxd -i build/cat.elf > src/kernel/cat_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/wc.c \
    -o build/wc.elf
xxd -i build/wc.elf > src/kernel/wc_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/head.c \
    -o build/head.elf
xxd -i build/head.elf > src/kernel/head_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/cp.c \
    -o build/cp.elf
xxd -i build/cp.elf > src/kernel/cp_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/mv.c \
    -o build/mv.elf
xxd -i build/mv.elf > src/kernel/mv_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -nostdlib -nostartfiles -fno-builtin -fno-pic -no-pie \
-T src/programs/user.ld src/programs/edit.c \
    -o build/edit.elf
xxd -i build/edit.elf > src/kernel/edit_elf_data.h
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/semaphore.c  -o build/semaphore.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/pipe.c       -o build/pipe.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/condvar.c    -o build/condvar.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/shm.c        -o build/shm.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/vfs.c        -o build/vfs.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/mfs4.c       -o build/mfs4.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/mq.c         -o build/mq.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/rtl8139.c   -o build/rtl8139.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/net.c        -o build/net.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/crypto.c     -o build/crypto.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/tls13.c      -o build/tls13.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/websocket.c  -o build/websocket.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/virtio_blk.c -o build/virtio_blk.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/x509.c       -o build/x509.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/ext2.c       -o build/ext2.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/apic.c       -o build/apic.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/acpi.c       -o build/acpi.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/smp.c        -o build/smp.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/device.c     -o build/device.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/drv_vga.c    -o build/drv_vga.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/drv_kbd.c    -o build/drv_kbd.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/vbe.c        -o build/vbe.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/graphics.c   -o build/graphics.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/kernel.c    -o build/kernel.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/idt.c       -o build/idt.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/pic.c       -o build/pic.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/keyboard.c  -o build/keyboard.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/shell.c     -o build/shell.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/memory.c    -o build/memory.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/timer.c     -o build/timer.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/fs.c        -o build/fs.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/paging.c    -o build/paging.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/task.c      -o build/task.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/syscall.c   -o build/syscall.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/tss.c       -o build/tss.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/vmm.c       -o build/vmm.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/elf_loader.c -o build/elf_loader.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/ipc.c        -o build/ipc.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/ata.c        -o build/ata.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/serial.c     -o build/serial.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/mouse.c      -o build/mouse.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/window.c     -o build/window.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/taskbar.c    -o build/taskbar.o
x86_64-linux-gnu-gcc -m64 -mno-red-zone -mcmodel=small -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -mno-sse -mno-sse2 -mno-mmx -c src/kernel/rtc.c          -o build/rtc.o
x86_64-linux-gnu-ld -m elf_x86_64 -T src/kernel/linker.ld build/kernel_entry.o build/isr.o build/kernel.o build/idt.o build/pic.o build/keyboard.o build/shell.o build/memory.o build/timer.o build/fs.o build/paging.o build/task.o build/syscall.o build/tss.o build/vmm.o build/elf_loader.o build/ipc.o build/semaphore.o build/pipe.o build/condvar.o build/shm.o build/vfs.o build/mfs4.o build/mq.o build/rtl8139.o build/net.o build/apic.o build/acpi.o build/smp.o build/device.o build/drv_vga.o build/drv_kbd.o build/vbe.o build/graphics.o build/ata.o build/serial.o build/mouse.o build/window.o build/taskbar.o build/rtc.o build/crypto.o build/tls13.o build/websocket.o build/virtio_blk.o build/x509.o build/ext2.o -o build/kernel.elf
x86_64-linux-gnu-objcopy -O binary build/kernel.elf build/kernel.bin
echo done
"@
    # Hapus header lama dari src/kernel/ agar tidak terkunci VS Code Language Server.
    # File baru ditulis ke build/ (NTFS watch-free), kernel dikompilasi dengan -Ibuild.
    Get-ChildItem "$SRC\kernel" -Filter "*_elf_data.h" -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Remove-Item "$SRC\kernel\smp_trampoline_bin_data.h" -Force -ErrorAction SilentlyContinue

    $wslReady = $wslScript -replace "`r`n", "`n" `
        -replace '> src/kernel/([a-z0-9_]+_(elf|bin)_data\.h)', '> build/$1' `
        -replace '(-mno-mmx) (-c src/kernel/)', '$1 -Ibuild $2'
    $result = wsl -e bash -c $wslReady 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Kernel build gagal!" -ForegroundColor Red
        Write-Host $result
        exit 1
    }
    Write-Host "       --> $BUILD\kernel.bin ($((Get-Item "$BUILD\kernel.bin").Length) bytes)" -ForegroundColor Green
}

function Build-Image {
    $boot_bin   = "$BUILD\boot.bin"
    $kernel_bin = "$BUILD\kernel.bin"
    $os_img     = "$BUILD\os.img"

    Write-Host "[IMG]  Creating OS image..." -ForegroundColor Cyan
    $boot_bytes   = [System.IO.File]::ReadAllBytes($boot_bin)
    $kernel_bytes = [System.IO.File]::ReadAllBytes($kernel_bin)
    $target       = 2 * 1024 * 1024  # 2MB

    # Tulis langsung ke file tanpa membuat array besar di memory
    $fs = [System.IO.FileStream]::new($os_img, [System.IO.FileMode]::Create)
    $fs.Write($boot_bytes,   0, $boot_bytes.Length)
    $fs.Write($kernel_bytes, 0, $kernel_bytes.Length)
    $remaining = $target - $boot_bytes.Length - $kernel_bytes.Length
    $zeros = New-Object byte[] 4096
    while ($remaining -gt 0) {
        $chunk = [Math]::Min($remaining, 4096)
        $fs.Write($zeros, 0, $chunk)
        $remaining -= $chunk
    }
    $fs.Close()

    Write-Host "[DONE] OS image siap: build\os.img ($target bytes, $($target/512) sektor)" -ForegroundColor Green
}

function Run-QEMU {
    $os_img   = "$BUILD\os.img"
    $disk_img = "$BUILD\disk.img"

    if (-not (Test-Path $os_img)) {
        Write-Host "[ERROR] OS image belum ada. Jalankan: .\build.ps1 build" -ForegroundColor Red
        exit 1
    }

    # Buat disk.img kosong (8MB) jika belum ada
    if (-not (Test-Path $disk_img)) {
        $zeros = New-Object byte[] (8 * 1024 * 1024)
        [System.IO.File]::WriteAllBytes($disk_img, $zeros)
        Write-Host "[DISK]  disk.img blank 8MB dibuat" -ForegroundColor Green
    }

    Write-Host "[QEMU] Menjalankan OS di emulator..." -ForegroundColor Cyan
    Write-Host "       Tekan Ctrl+Alt+G untuk release mouse dari QEMU" -ForegroundColor Yellow
    Write-Host "       Zoom: View > Zoom In/Out di menu QEMU (GTK)" -ForegroundColor Yellow
    Write-Host "       Serial COM1: output serial tampil di jendela PowerShell ini" -ForegroundColor DarkGray
    & $QEMU -machine pc `
        -smp 2 `
        -drive format=raw,file=$os_img,index=0,if=ide `
        -drive format=raw,file=$disk_img,index=1,if=ide `
        -netdev user,id=net0 -device rtl8139,netdev=net0 `
        -serial stdio `
        -display gtk,zoom-to-fit=on
}

function Clean-Build {
    Write-Host "[CLEAN] Membersihkan build files..." -ForegroundColor Cyan
    Remove-Item "$BUILD\*.bin", "$BUILD\*.o", "$BUILD\*.img" -ErrorAction SilentlyContinue
    Write-Host "[DONE] Build folder bersih." -ForegroundColor Green
}

# --- Main ---
switch ($Action.ToLower()) {
    "build" {
        Write-Host "=== Building Oria OS ===" -ForegroundColor White
        Build-Kernel
        Build-Bootloader
        Build-Image
    }
    "run" {
        Write-Host "=== Building & Running Oria OS ===" -ForegroundColor White
        Build-Kernel
        Build-Bootloader
        Build-Image
        Run-QEMU
    }
    "clean" {
        Clean-Build
    }
    default {
        Write-Host "Usage: .\build.ps1 [build|run|clean]" -ForegroundColor Yellow
    }
}
