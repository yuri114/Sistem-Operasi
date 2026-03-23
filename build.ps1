# ============================================
# build.ps1 - Build Script untuk MyOS
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
$QEMU    = "C:\Program Files\qemu\qemu-system-i386.exe"

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
nasm -f elf32 src/kernel/kernel_entry.asm -o build/kernel_entry.o
nasm -f elf32 src/kernel/isr.asm         -o build/isr.o
gcc -m32 -nostdlib -nostartfiles -fno-builtin -fno-pic \
-T src/programs/user.ld src/programs/hello.c \
    -o build/hello.elf
xxd -i build/hello.elf > src/kernel/hello_elf_data.h
gcc -m32 -nostdlib -nostartfiles -fno-builtin -fno-pic \
-T src/programs/user.ld src/programs/sender.c \
    -o build/sender.elf
xxd -i build/sender.elf > src/kernel/sender_elf_data.h
gcc -m32 -nostdlib -nostartfiles -fno-builtin -fno-pic \
-T src/programs/user.ld src/programs/writer.c \
    -o build/writer.elf
xxd -i build/writer.elf > src/kernel/writer_elf_data.h
gcc -m32 -nostdlib -nostartfiles -fno-builtin -fno-pic \
-T src/programs/user.ld src/programs/piper.c \
    -o build/piper.elf
xxd -i build/piper.elf > src/kernel/piper_elf_data.h
gcc -m32 -nostdlib -nostartfiles -fno-builtin -fno-pic \
-T src/programs/user.ld src/programs/pipe_sender.c \
    -o build/pipe_sender.elf
xxd -i build/pipe_sender.elf > src/kernel/pipe_sender_elf_data.h
gcc -m32 -nostdlib -nostartfiles -fno-builtin -fno-pic \
-T src/programs/user.ld src/programs/pipe_receiver.c \
    -o build/pipe_receiver.elf
xxd -i build/pipe_receiver.elf > src/kernel/pipe_receiver_elf_data.h
gcc -m32 -nostdlib -nostartfiles -fno-builtin -fno-pic \
-T src/programs/user.ld src/programs/devtest.c \
    -o build/devtest.elf
xxd -i build/devtest.elf > src/kernel/devtest_elf_data.h
gcc -m32 -nostdlib -nostartfiles -fno-builtin -fno-pic \
-T src/programs/user.ld src/programs/gfxtest.c \
    -o build/gfxtest.elf
xxd -i build/gfxtest.elf > src/kernel/gfxtest_elf_data.h
gcc -m32 -nostdlib -nostartfiles -fno-builtin -fno-pic \
-T src/programs/user.ld src/programs/gui_demo.c \
    -o build/gui_demo.elf
xxd -i build/gui_demo.elf > src/kernel/gui_demo_elf_data.h
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/semaphore.c  -o build/semaphore.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/pipe.c       -o build/pipe.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/device.c     -o build/device.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/drv_vga.c    -o build/drv_vga.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/drv_kbd.c    -o build/drv_kbd.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/vbe.c        -o build/vbe.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/graphics.c   -o build/graphics.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/kernel.c    -o build/kernel.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/idt.c       -o build/idt.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/pic.c       -o build/pic.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/keyboard.c  -o build/keyboard.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/shell.c     -o build/shell.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/memory.c    -o build/memory.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/timer.c     -o build/timer.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/fs.c        -o build/fs.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/paging.c    -o build/paging.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/task.c      -o build/task.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/syscall.c   -o build/syscall.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/tss.c       -o build/tss.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/vmm.c       -o build/vmm.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/elf_loader.c -o build/elf_loader.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/ipc.c        -o build/ipc.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/ata.c        -o build/ata.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/mouse.c      -o build/mouse.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/window.c     -o build/window.o
ld -m elf_i386 -T src/kernel/linker.ld build/kernel_entry.o build/isr.o build/kernel.o build/idt.o build/pic.o build/keyboard.o build/shell.o build/memory.o build/timer.o build/fs.o build/paging.o build/task.o build/syscall.o build/tss.o build/vmm.o build/elf_loader.o build/ipc.o build/semaphore.o build/pipe.o build/device.o build/drv_vga.o build/drv_kbd.o build/vbe.o build/graphics.o build/ata.o build/mouse.o build/window.o -o build/kernel.elf
objcopy -O binary build/kernel.elf build/kernel.bin
echo done
"@
    $result = wsl -e bash -c ($wslScript -replace "`r`n", "`n") 2>&1
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

    # Buat disk.img kosong (1MB) jika belum ada
    if (-not (Test-Path $disk_img)) {
        $zeros = New-Object byte[] (1024 * 1024)
        [System.IO.File]::WriteAllBytes($disk_img, $zeros)
        Write-Host "[DISK]  disk.img blank 1MB dibuat" -ForegroundColor Green
    }

    Write-Host "[QEMU] Menjalankan OS di emulator..." -ForegroundColor Cyan
    Write-Host "       Tekan Ctrl+Alt+G untuk release mouse dari QEMU" -ForegroundColor Yellow
    Write-Host "       Zoom: View > Zoom In/Out di menu QEMU (GTK)" -ForegroundColor Yellow
    & $QEMU -machine pc `
        -drive format=raw,file=$os_img,index=0,if=ide `
        -drive format=raw,file=$disk_img,index=1,if=ide `
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
        Write-Host "=== Building MyOS ===" -ForegroundColor White
        Build-Kernel
        Build-Bootloader
        Build-Image
    }
    "run" {
        Write-Host "=== Building & Running MyOS ===" -ForegroundColor White
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
