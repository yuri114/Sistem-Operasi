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
        Write-Host "        Buat file src\boot\boot.asm dulu!" -ForegroundColor Yellow
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

function Build-Image {
    $boot_bin = "$BUILD\boot.bin"
    $os_img   = "$BUILD\os.img"

    Write-Host "[IMG]  Creating OS image..." -ForegroundColor Cyan
    Copy-Item $boot_bin $os_img -Force
    Write-Host "[DONE] OS image siap: build\os.img" -ForegroundColor Green
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
        Build-Image
    }
    "run" {
        Write-Host "=== Building & Running MyOS ===" -ForegroundColor White
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
