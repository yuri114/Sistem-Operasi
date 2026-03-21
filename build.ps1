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
    $input_file  = "$SRC\boot\boot.asm"
    $output_file = "$BUILD\boot.bin"

    if (-not (Test-Path $input_file)) {
        Write-Host "[ERROR] File tidak ditemukan: $input_file" -ForegroundColor Red
        exit 1
    }

    Write-Host "[NASM] Compiling bootloader..." -ForegroundColor Cyan
    & $NASM -f bin $input_file -o $output_file

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
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/kernel.c   -o build/kernel.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/idt.c      -o build/idt.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/pic.c      -o build/pic.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/keyboard.c -o build/keyboard.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/shell.c    -o build/shell.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/memory.c   -o build/memory.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/timer.c    -o build/timer.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/fs.c       -o build/fs.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/paging.c   -o build/paging.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/task.c      -o build/task.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/syscall.c   -o build/syscall.o
gcc -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic -c src/kernel/tss.c       -o build/tss.o

ld -m elf_i386 -T src/kernel/linker.ld build/kernel_entry.o build/isr.o build/kernel.o build/idt.o build/pic.o build/keyboard.o build/shell.o build/memory.o build/timer.o build/fs.o build/paging.o build/task.o build/syscall.o build/tss.o -o build/kernel.elf
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
    $target       = 64 * 512   # 32KB = 64 sektor
    $padding      = New-Object byte[] ($target - $boot_bytes.Length - $kernel_bytes.Length)
    $img          = $boot_bytes + $kernel_bytes + $padding
    [System.IO.File]::WriteAllBytes($os_img, $img)
    Write-Host "[DONE] OS image siap: build\os.img ($($img.Length) bytes, $($img.Length/512) sektor)" -ForegroundColor Green
}

function Run-QEMU {
    $os_img = "$BUILD\os.img"

    if (-not (Test-Path $os_img)) {
        Write-Host "[ERROR] OS image belum ada. Jalankan: .\build.ps1 build" -ForegroundColor Red
        exit 1
    }

    Write-Host "[QEMU] Menjalankan OS di emulator..." -ForegroundColor Cyan
    Write-Host "       Tekan Ctrl+Alt+G untuk release mouse dari QEMU" -ForegroundColor Yellow
    & $QEMU -drive format=raw,file=$os_img
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
        Build-Bootloader
        Build-Kernel
        Build-Image
    }
    "run" {
        Write-Host "=== Building & Running MyOS ===" -ForegroundColor White
        Build-Bootloader
        Build-Kernel
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
