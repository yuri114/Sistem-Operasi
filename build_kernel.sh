#!/bin/bash

GCC=/usr/bin/gcc
LD=/usr/bin/ld

cp "/d/Sistem Operasi/src/kernel/kernel.c" /tmp/kernel.c
cp "/d/Sistem Operasi/src/kernel/kernel_entry.asm" /tmp/kernel_entry.asm
cp "/d/Sistem Operasi/src/kernel/linker.ld" /tmp/linker.ld

echo "[NASM] Compiling kernel_entry.asm..."
nasm -f elf32 /tmp/kernel_entry.asm -o /tmp/kernel_entry.o
echo "       OK"

echo "[GCC]  Compiling kernel.c..."
$GCC -m32 -ffreestanding -fno-builtin -nostdlib -nostartfiles -fno-pic \
    -c /tmp/kernel.c -o /tmp/kernel.o
echo "       OK"

echo "[LD]   Linking..."
$LD -m elf_i386 -T /tmp/linker.ld \
    /tmp/kernel_entry.o /tmp/kernel.o \
    -o /tmp/kernel.bin
echo "       OK"

cp /tmp/kernel.bin "/d/Sistem Operasi/build/kernel.bin"
echo "[DONE] build/kernel.bin siap ($(wc -c < /tmp/kernel.bin) bytes)"

