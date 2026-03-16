# ============================================
# Makefile untuk MyOS
# Jalankan dari MSYS2 atau via build.ps1
# ============================================

# --- Tools ---
NASM    = nasm
GCC     = i686-w64-mingw32-gcc
LD      = i686-w64-mingw32-ld
OBJCOPY = i686-w64-mingw32-objcopy
QEMU    = qemu-system-i386

# --- Flags ---
GCC_FLAGS = -ffreestanding -m32 -nostdlib -nostartfiles -nodefaultlibs \
            -fno-builtin -Wall -Wextra -O2

LD_FLAGS  = -melf_i386 -T src/kernel/linker.ld --oformat binary

# --- File Output ---
BOOT_BIN  = build/boot.bin
KERNEL_BIN = build/kernel.bin
OS_IMG    = build/os.img

# --- Default target ---
all: $(OS_IMG)

# --- Compile Bootloader ---
$(BOOT_BIN): src/boot/boot.asm
	@echo "[NASM] Compiling bootloader..."
	$(NASM) -f bin $< -o $@
	@echo "       --> $(BOOT_BIN)"

# --- Compile Kernel (C files) ---
build/kernel.o: src/kernel/kernel.c
	@echo "[GCC]  Compiling kernel..."
	$(GCC) $(GCC_FLAGS) -c $< -o $@

# --- Link Kernel ---
$(KERNEL_BIN): build/kernel.o
	@echo "[LD]   Linking kernel..."
	$(LD) $(LD_FLAGS) $< -o $@

# --- Gabungkan Bootloader + Kernel jadi image OS ---
$(OS_IMG): $(BOOT_BIN)
	@echo "[IMG]  Creating OS image..."
	cp $(BOOT_BIN) $(OS_IMG)
	@echo "[DONE] OS image: $(OS_IMG)"

# --- Jalankan di QEMU ---
run: $(OS_IMG)
	@echo "[QEMU] Starting OS..."
	$(QEMU) -drive format=raw,file=$(OS_IMG) -monitor stdio

# --- Bersihkan hasil build ---
clean:
	@echo "[CLEAN] Removing build files..."
	rm -f build/*.bin build/*.o build/*.img

.PHONY: all run clean
