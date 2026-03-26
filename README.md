# MyOS

Sistem operasi *from-scratch* berbasis x86_64 yang ditulis dalam Assembly (NASM) dan C, berjalan di **64-bit Long Mode**, dengan layar GUI 1920×1080 32bpp, multitasking preemptive, filesystem sendiri, dan window manager sederhana.

> Proyek ini adalah OS riil yang berjalan di QEMU — bukan emulasi, bukan simulator — dibangun sepenuhnya dari BIOS boot hingga GUI multi-window.

---

## Tangkapan Layar

> *(jalankan `.\build.ps1 run` untuk melihat tampilan)*

---

## Spesifikasi Teknis

| Komponen | Detail |
|---|---|
| Arsitektur | x86_64 — IA-32e Long Mode (64-bit) |
| Boot | MBR 512-byte → Protected Mode → Long Mode |
| Resolusi | 1920×1080 @ 32bpp (VBE Linear Framebuffer, ~8.3MB) |
| Kernel | ~178 KB binary |
| Memory Map | 4-level paging, identity-mapped 4GB, heap kernel 2MB |
| Multitasking | Round-robin preemptive, hingga 16 task, PIT IRQ0 |
| Ring | Kernel Ring-0 / User Ring-3 (isolasi penuh per-proses) |
| Filesystem | MFS2 — 32 file × 16KB, ATA PIO |
| Syscall | `int 0x80` — 49 syscall tersedia |
| Build | WSL + `x86_64-linux-gnu-gcc` + NASM, output `os.img` binary raw |
| Emulator | QEMU `qemu-system-x86_64` |

---

## Fitur yang Sudah Berjalan

### Kernel Core
- **Boot 64-bit**: BIOS → Protected Mode → 4-level paging (PML4) → Long Mode
- **GDT 64-bit**: kernel CS/DS (ring-0), user CS/DS (ring-3), TSS64 (16-byte descriptor)
- **IDT 64-bit**: 16-byte gate descriptor, 49 entri (exception 0–14, IRQ 0/1/12, syscall 0x80)
- **Heap kernel**: first-fit allocator dengan coalescing free, interrupt-safe (`pushfq`/`popfq`)
- **PMM**: bitmap 4096 frame (16MB), frame 0–767 pre-reserved untuk kernel
- **VMM 4-level**: `vmm_create_page_dir`, `vmm_map_page` (walk PML4→PDPT→PD→PT), `vmm_free_user_memory`

### Multitasking
- **Preemptive round-robin** via PIT IRQ0 (100 Hz)
- **Context switch 64-bit**: 15 register GPR (rax–r15) disimpan di stack, iret frame
- **TSS64**: `rsp0` diperbarui setiap task switch → stack ring-0 per-task yang benar
- **Ring-3 isolation**: setiap proses memiliki PML4 sendiri + RSP user di 0x600000
- **ELF64 loader**: `vmm_map_page` per segment, load address 0x400000

### Filesystem
- **MFS2**: custom raw filesystem, 32 file simultan, 16KB per file
- **ATA PIO**: baca/tulis sector ke disk image kedua (`disk.img`)
- **Syscall**: `SYS_FS_READ`, `SYS_FS_WRITE`, `SYS_FS_LIST`, `SYS_FS_DELETE`

### Driver & I/O
- **VBE** (Bochs/QEMU stdvga): deteksi LFB via PCI BAR0, set mode 1920×1080×32
- **Keyboard**: PS/2 scancode → ASCII, ring buffer, tab-completion di shell
- **Mouse**: PS/2 protokol 3-byte, koordinat relatif + tombol L/R
- **PIC**: cascade 8259A, remapped IRQ 0–15 ke INT 32–47
- **PIT**: timer 100 Hz, `get_ticks()`
- **ATA**: PIO mode, polling BSY, read/write sector

### GUI & Window Manager
- **Graphics**: `draw_pixel`, `fill_rect`, `draw_line`, font 8×8 pixel
- **Window Manager**: hingga 16 window simultan, drag, close, minimize/restore
- **Taskbar**: task list, klik untuk focus/restore window
- **Klip mouse**: tracking posisi, kursor paint sederhana

### Shell
- **Command-line shell** interaktif di kernel thread
- **History**: 8 entri, navigasi dengan ↑/↓
- **Tab-completion**: auto-complete perintah dan nama file
- **Pipe operator**: `prog1 | prog2` (menggunakan kernel pipe buffer)
- **Built-in commands**: `ps`, `kill`, `ls`, `cat`, `write`, `del`, `clear`, `help`, `exec`, ...

### Syscall Interface (user space)
```
SYS_PRINT       SYS_GETKEY      SYS_EXIT        SYS_ALLOC       SYS_FREE
SYS_FS_READ     SYS_FS_WRITE    SYS_FS_LIST     SYS_FS_DELETE
SYS_MSG_SEND    SYS_MSG_RECV    SYS_KILL        SYS_GETPID
SYS_SEM_*       SYS_PIPE_*      SYS_DEV_*
SYS_DRAW_PIXEL  SYS_FILL_RECT   SYS_DRAW_LINE   SYS_DRAW_STR
SYS_WIN_CREATE  SYS_WIN_DRAW    SYS_WIN_EVENT   SYS_WIN_BTN_ADD ...
SYS_MOUSE_GET   SYS_GET_TICKS   SYS_YIELD       SYS_SLEEP       SYS_EXEC
```

### Program Bawaan (user space, ELF64)
| Program | Deskripsi |
|---|---|
| `paint` | Aplikasi gambar dengan mouse, 16 warna |
| `notepad` | Editor teks sederhana dengan keyboard input |
| `calc` | Kalkulator ekspresi dasar |
| `filemanager` | Browser file MFS2 GUI |
| `gui_term` | Terminal emulator dalam window GUI |
| `hello` | Hello-world demo user process |
| `gfxtest` | Demo grafis (pixel, rect, line) |
| `gui_demo` | Demo window manager |
| `sender` / `piper` | Demo IPC dan pipe antar proses |

---

## Struktur Direktori

```
.
├── build.ps1                 # Build script utama (PowerShell + WSL)
├── ROADMAP.txt               # Roadmap pengembangan lengkap
├── src/
│   ├── boot/
│   │   └── boot.asm          # MBR bootloader (512 byte, BIOS INT 13h LBA)
│   ├── kernel/
│   │   ├── kernel_entry.asm  # Setup 4-level paging + Long Mode entry [BITS 32→64]
│   │   ├── linker.ld         # Linker script (ELF64, . = 0x8000)
│   │   ├── isr.asm           # ISR wrapper 64-bit (SAVE_REGS 15 GPR)
│   │   ├── idt.h / idt.c     # IDT 64-bit (16-byte gate descriptor)
│   │   ├── tss.h / tss.c     # TSS64 + GDT descriptor 16-byte
│   │   ├── task.h / task.c   # Multitasking, context-switch, iretq frame
│   │   ├── vmm.h / vmm.c     # PMM bitmap + VMM 4-level paging
│   │   ├── paging.h / paging.c  # Wrapper paging (sebagian no-op di LM)
│   │   ├── elf_loader.h / elf_loader.c  # ELF64 loader ke per-proses PML4
│   │   ├── memory.h / memory.c  # Heap kernel (malloc/free first-fit)
│   │   ├── kernel.c          # kernel_main(), shell loop, exception handler
│   │   ├── syscall.h / syscall.c  # Dispatch syscall int 0x80
│   │   ├── shell.h / shell.c # Shell CLI interaktif
│   │   ├── vbe.h / vbe.c     # VBE mode setting + PCI BAR0 discovery
│   │   ├── graphics.h / graphics.c  # Framebuffer 32bpp primitif
│   │   ├── window.h / window.c      # Window manager
│   │   ├── taskbar.h / taskbar.c    # Taskbar
│   │   ├── keyboard.h / keyboard.c  # PS/2 keyboard + ring buffer
│   │   ├── mouse.h / mouse.c        # PS/2 mouse
│   │   ├── timer.h / timer.c        # PIT 100Hz
│   │   ├── pic.h / pic.c            # 8259A PIC cascade
│   │   ├── fs.h / fs.c              # Filesystem MFS2
│   │   ├── ata.h / ata.c            # ATA PIO driver
│   │   ├── ipc.h / ipc.c            # Message passing antar proses
│   │   ├── semaphore.h / semaphore.c
│   │   ├── pipe.h / pipe.c          # Pipe buffer kernel
│   │   ├── device.h / device.c      # Device framework
│   │   ├── drv_vga.c / drv_kbd.c    # VGA & keyboard device driver
│   │   └── *_elf_data.h      # Program user ter-embed sebagai C array
│   └── programs/
│       ├── lib.h             # Syscall wrapper + stdlib minimal untuk user space
│       ├── user.ld           # Linker script user (ELF64, . = 0x400000)
│       ├── paint.c           # Aplikasi paint
│       ├── notepad.c         # Editor teks
│       ├── calc.c            # Kalkulator
│       ├── filemanager.c     # File manager GUI
│       ├── gui_term.c        # Terminal GUI
│       └── ...               # Program demo lainnya
└── build/
    ├── os.img                # Disk image final (2MB, sektor raw)
    ├── disk.img              # Disk data sekunder (8MB, filesystem MFS2)
    └── kernel.bin            # Kernel binary (~178KB)
```

---

## Cara Build & Jalankan

### Prasyarat
- **Windows 10/11** dengan WSL (Ubuntu/Debian)
- **WSL packages**: `nasm`, `x86_64-linux-gnu-gcc`, `x86_64-linux-gnu-binutils`
- **Windows**: [QEMU for Windows](https://www.qemu.org/download/#windows) — `qemu-system-x86_64.exe`

```bash
# Install tools di WSL
sudo apt update
sudo apt install nasm gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu
```

### Build

```powershell
# Di PowerShell (D:\Sistem Operasi)
.\build.ps1 build
```

Output: `build/os.img` (2MB raw disk image)

### Jalankan

```powershell
.\build.ps1 run
```

Membuka QEMU dengan `qemu-system-x86_64`, layar 1920×1080, GUI langsung muncul.

### Clean

```powershell
.\build.ps1 clean
```

---

## Memory Map

```
Alamat Fisik    Ukuran    Isi
────────────────────────────────────────────────────────────────
0x00000–0x007FF   2KB     Real Mode IVT + BDA
0x07C00–0x07DFF 512B     Bootloader MBR (boot.asm)
0x08000–0x2FFFF ~160KB   Kernel binary (kernel_entry + kode C)
0x30000–0x8FFFF ~384KB   Stack kernel (tumbuh dari 0x90000 ke bawah)
0x100000–0x2FFFFF 2MB    Heap kernel (malloc/free)
0x300000–0x3FFFFF  1MB   (reserved — dulu ELF loader, kini bebas)
0x400000–0x4FFFFF  1MB   User ELF load address (setiap proses)
0x600000–0x600FFF  4KB   User stack (setiap proses, RSP = 0x601000)
0x3000000+        ...    PMM frame pool (0x300000 ke atas dipakai proses)

Boot page tables (sementara, dipakai kernel_entry.asm):
0x1000  PML4[512]
0x2000  PDPT[512]    → 4×PD di bawah
0x3000  PD[0–1GB]    2MB pages, User-accessible (flag 0x87)
0x4000  PD[1–2GB]    2MB pages, kernel-only (flag 0x83)
0x5000  PD[2–3GB]    2MB pages, kernel-only
0x6000  PD[3–4GB]    2MB pages, kernel-only (VBE LFB ~0xE0000000)
```

---

## Arsitektur Long Mode

```
┌─────────────────────────────────────────────────────┐
│  BIOS / SeaBIOS                                      │
│    INT 13h LBA → load kernel ke 0x8000              │
└──────────────┬──────────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────────┐
│  boot.asm  [16-bit Real Mode]                        │
│    → lgdt (GDT 32-bit flat) → CR0.PE=1              │
│    → jmp 0x08:pm_entry                              │
└──────────────┬──────────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────────┐
│  kernel_entry.asm  [BITS 32 — Protected Mode]        │
│    1. Zero BSS                                       │
│    2. Build PML4/PDPT/4×PD @ 0x1000–0x6000          │
│    3. CR4.PAE=1, CR3=0x1000, EFER.LME=1             │
│    4. lgdt (GDT64), CR0.PG=1                         │
│    5. jmp 0x08:long_mode_entry                       │
└──────────────┬──────────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────────┐
│  kernel_entry.asm  [BITS 64 — Long Mode]             │
│    → reload DS/ES/SS=0x10, RSP=0x90000               │
│    → call kernel_main()                              │
└──────────────┬──────────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────────┐
│  kernel.c  kernel_main()                             │
│    paging_init → vbe_find_lfb → vbe_set_mode         │
│    graphics_init → shell_init → mem_init → pmm_init  │
│    ata_init → fs_init → ipc/sem/pipe init            │
│    dev_register → programs_init                      │
│    timer_init → pic_init → idt_init                  │
│    task_init → tss64_init → idt_set_gate_user(0x80)  │
│    sti → shell polling loop                          │
└─────────────────────────────────────────────────────┘
```

---

## Sistem Syscall

User program memanggil syscall via `int 0x80`:

```c
// lib.h — contoh penggunaan dari user space
draw_pixel(100, 200, GFX_RED);     // syscall SYS_DRAW_PIXEL
int w = win_create("Paint", ...);  // syscall SYS_WIN_CREATE
char c = getkey();                 // syscall SYS_GETKEY
void *buf = malloc(1024);          // syscall SYS_ALLOC
```

Register convention:
```
RAX = nomor syscall
RBX = argumen 1 (atau pointer ke struct)
RDX = argumen 2
Return value → RAX
```

---

## Roadmap

Lihat [ROADMAP.txt](ROADMAP.txt) untuk roadmap lengkap.

**Milestone yang sudah selesai:**

| Milestone | Tanggal | Catatan |
|---|---|---|
| Foundation | Mar 2026 | Heap, multitasking, FS, IPC, GUI, shell |
| Tahap A — 32bpp True Color | 24 Mar 2026 | Framebuffer 32-bit |
| Tahap B — 1280×720 | 26 Mar 2026 | HD Ready |
| Tahap B+ — 1920×1080 | 26 Mar 2026 | Full HD, dual page table VBE |
| Tahap C — 64-bit Long Mode | 26 Mar 2026 | Rewrite penuh ke x86_64 |

**Yang direncanakan berikutnya:**

- **Tahap D** — SYSCALL/SYSRET, PMM lebih besar, Serial debug, ATA DMA
- **Tahap E** — Filesystem MFS2 lebih besar (subdirektori), journaling
- **Tahap F** — Shell redirect/env var, libc minimal, lebih banyak program
- **Tahap G** — Network stack (RTL8139/E1000, TCP/IP minimal)
- **Tahap H** — SMP multi-core

---

## Lisensi

Proyek ini bersifat edukatif. Bebas digunakan untuk belajar dan referensi.
