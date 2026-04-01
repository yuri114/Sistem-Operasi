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
| Kernel | ~224 KB binary |
| Memory Map | 4-level paging, identity-mapped 4GB, heap kernel 6MB (0x100000–0x6FFFFF) |
| Multitasking | Round-robin preemptive, hingga 16 task, PIT IRQ0 @ 1000 Hz |
| Ring | Kernel Ring-0 / User Ring-3 (isolasi penuh per-proses) |
| Filesystem | MFS3 — 64 file × 64KB, pointer-based, dirty/tmpfs/perms/timestamp, ATA PIO |
| Syscall | `SYSCALL/SYSRET` (IA32_LSTAR MSR) — 55 syscall tersedia |
| Build | WSL + `x86_64-linux-gnu-gcc` + NASM, output `os.img` binary raw |
| Emulator | QEMU `qemu-system-x86_64` |

---

## Fitur yang Sudah Berjalan

### Kernel Core
- **Boot 64-bit**: BIOS → Protected Mode → 4-level paging (PML4) → Long Mode
- **GDT 64-bit**: kernel CS/DS (ring-0), user CS/DS (ring-3), TSS64 (16-byte descriptor)
- **IDT 64-bit**: 16-byte gate descriptor, exception 0–14, IRQ 0/1/12
- **Heap kernel**: first-fit allocator dengan coalescing free, interrupt-safe (`pushfq`/`popfq`), 6MB (0x100000–0x6FFFFF)
- **PMM**: bitmap 16384 frame (64MB), frame 0–767 pre-reserved untuk kernel
- **VMM 4-level**: `vmm_create_page_dir`, `vmm_map_page` (walk PML4→PDPT→PD→PT), `vmm_free_user_memory`
- **Demand paging**: `#PF` handler — page not-present di user space → `pmm_alloc_frame` + zero-fill + map, retry

### Multitasking
- **Preemptive round-robin** via PIT IRQ0 (1000 Hz, 1 tick = 1 ms)
- **Context switch 64-bit**: 15 register GPR (rax–r15) disimpan di stack, iretq frame
- **TSS64**: `rsp0` diperbarui setiap task switch → stack ring-0 per-task yang benar
- **Ring-3 isolation**: setiap proses memiliki PML4 sendiri + RSP user di 0x600000
- **ELF64 loader**: `vmm_map_page` per segment, load address 0x400000
- **task_sleep(ms)**: sleep akurat berbasis tick (TASK_SLEEPING + wake_tick)

### Filesystem — MFS3
- **MFS3**: custom raw filesystem, 64 file × 64KB per file, pointer-based (data di heap)
- **Fitur lanjutan**: dirty bit + `fs_flush()`, tmpfs flag (ramdisk), permission bits (R/W/X/DIR), timestamp (ctime/mtime)
- **Subdirektori**: `fs_mkdir()`, `fs_list_dir()`, `fs_get_perms()`, `fs_set_perms()`
- **ATA PIO**: baca/tulis sector ke disk image (`disk.img`), auto-migrate MFS2→MFS3
- **Syscall**: `SYS_FS_READ/WRITE/LIST/DELETE/SYNC/TMPWRITE/MKDIR`

### Driver & I/O
- **VBE** (Bochs/QEMU stdvga): deteksi LFB via PCI BAR0, set mode 1920×1080×32
- **Keyboard**: PS/2 scancode → ASCII, ring buffer, tab-completion di shell, Ctrl/Alt/CapsLock/F1-F10
- **Mouse**: PS/2 protokol 3-byte, koordinat relatif + tombol L/R
- **PIC**: cascade 8259A, remapped IRQ 0–15 ke INT 32–47
- **PIT**: timer 1000 Hz, `get_ticks()` (millisecond resolution)
- **ATA**: PIO mode + Bus Master DMA (BM-IDE via PCI BAR4), polling BSY
- **Serial**: COM1 debug output → QEMU `-serial stdio`

### IPC & Sinkronisasi
- **Message passing**: `SYS_MSG_SEND` / `SYS_MSG_RECV`
- **Semaphore**: `SYS_SEM_CREATE/WAIT/SIGNAL/DESTROY`
- **Pipe**: anonymous pipe buffer kernel + named pipe (`SYS_PIPE_NAMED`)
- **Shared memory**: `shm_create/attach/detach`, dipetakan ke VA 0x500000+slot×4096 (`SYS_SHM_CREATE/ATTACH/DETACH`)

### GUI & Window Manager
- **Graphics**: `draw_pixel`, `fill_rect`, `draw_line`, font 8×8 pixel
- **Window Manager**: hingga 16 window simultan, drag, close, minimize/restore
- **Taskbar**: task list, klik untuk focus/restore window
- **Klip mouse**: tracking posisi, kursor paint sederhana

### Networking — Tahap G
- **RTL8139 driver**: PCI scan (vendor 0x10EC / device 0x8139), I/O port access, TX 4-descriptor round-robin, RX ring 8K (polling, tanpa IRQ)
- **Ethernet**: bangun/parse frame 14-byte header, ethertype ARP/IPv4
- **ARP**: request/reply, cache 8 slot IP→MAC, jawab ARP request untuk IP kita
- **IPv4**: header 20B, TTL=64, checksum 16-bit one's complement
- **ICMP**: echo request (type 8) / echo reply (type 0), RTT diukur via `get_ticks()` (1ms)
- **Routing**: subnet check → direct atau via gateway (10.0.2.2)
- **QEMU SLIRP**: `-netdev user,id=net0 -device rtl8139,netdev=net0`
  - Guest IP: `10.0.2.15` (hardcoded), Gateway: `10.0.2.2`, DNS: `10.0.2.3`

### SMP — Tahap H
- **LAPIC**: enable via IA32_APIC_BASE MSR + SVR register, baca APIC ID, kirim INIT/SIPI IPI via ICR
- **ACPI MADT parser**: scan RSDP → RSDT → MADT untuk enumerasi CPU/APIC ID
- **AP trampoline** di 0x7000: real mode → 32-bit protected → 64-bit long mode
- **Per-AP stack**: 8KB per AP, dihitung dari LAPIC ID (AP1: 0x9D000, AP2: 0x9B000, ...)
- **Per-AP IDT**: setiap AP memanggil `idt_reload()` — load IDT BSP yang sudah diisi
- **Per-AP TSS**: GDT diperluas 8 slot TSS (CPU0=0x30 — CPU7=0xA0); `tss64_ap_init()` isi descriptor + `ltr`
- **Per-AP LAPIC**: `apic_enable()` dijalankan di setiap AP; AP menerima `sti`
- **Spinlock atomik**: `spinlock_acquire/release` via `__sync_lock_test_and_set`
- **Terverifikasi**: `cpu total: 2`, `ap online: 1/1` di QEMU `-smp 2`
- **Shell**: perintah `cpuinfo` menampilkan daftar CPU + APIC/ACPI ID

### Shell
- **Command-line shell** interaktif di kernel thread
- **History**: 8 entri, navigasi dengan ↑/↓
- **Tab-completion**: auto-complete perintah dan nama file
- **Pipe operator**: `prog1 | prog2` (menggunakan kernel pipe buffer)
- **Environment variables**: `export KEY=VAL`, ekspansi `$VAR` di input
- **Direktori**: `cd <dir>`, `pwd`, direktori-aware `ls`/`read`/`write`/`del`
- **Background exec**: tambah `&` di akhir perintah
- **Jaringan**: `ifconfig` (tampilkan MAC/IP/GW), `ping <ip>` (4 ICMP echo requests + RTT)
- **SMP info**: `cpuinfo` (jumlah CPU dari MADT + jumlah AP yang online)
- **Built-in commands**: `ps`, `kill`, `ls`, `read`, `write`, `del`, `clear`, `help`, `exec`, `sync`, `mkdir`, `chmod`, `cd`, `pwd`, `export`, `env`, `ifconfig`, `ping`, `cpuinfo`, ...

### Syscall Interface (user space via `SYSCALL/SYSRET`)
```
SYS_PRINT(1)    SYS_GETKEY(2)   SYS_EXIT(3)     SYS_ALLOC(4)    SYS_FREE(5)
SYS_FS_READ(6)  SYS_FS_WRITE(7) SYS_FS_LIST(8)  SYS_FS_DELETE(9)
SYS_MSG_SEND    SYS_MSG_RECV    SYS_KILL        SYS_GETPID
SYS_SEM_*       SYS_PIPE_*      SYS_DEV_*
SYS_DRAW_PIXEL  SYS_FILL_RECT   SYS_DRAW_LINE   SYS_DRAW_STR
SYS_WIN_CREATE  SYS_WIN_DRAW    SYS_WIN_EVENT   SYS_WIN_BTN_ADD ...
SYS_MOUSE_GET   SYS_GET_TICKS   SYS_YIELD       SYS_SLEEP       SYS_EXEC
SYS_FS_SYNC(49) SYS_FS_TMPWRITE(50) SYS_FS_MKDIR(51) SYS_PIPE_NAMED(52)
SYS_SHM_CREATE(53) SYS_SHM_ATTACH(54) SYS_SHM_DETACH(55)
```

### Libc Minimal (`lib.h`) — Tahap F2
- **Variadic**: `va_list`, `va_start`, `va_arg`, `va_end` (GCC builtins)
- **Format**: `vsprintf`, `sprintf`, `printf` — mendukung `%d %i %u %x %X %s %c %p %%`, flags `-` `0` width, modifier `l`
- **String extra**: `strcat`, `strstr`, `strtol`, `atoi`
- **Memory**: `memcpy`, `memset`, `memmove`, `memcmp`

### Program Bawaan (user space, ELF64)
| Program | Deskripsi |
|---|---|
| `paint` | Aplikasi gambar dengan mouse, 16 warna |
| `notepad` | Editor teks sederhana dengan keyboard input |
| `calc` | Kalkulator ekspresi dasar |
| `filemanager` | Browser file MFS3 GUI |
| `gui_term` | Terminal emulator dalam window GUI |
| `clock` | Widget jam — tampilkan uptime HH:MM:SS (update tiap detik) |
| `sysinfo` | Panel info sistem — PID, uptime, tick count, arsitektur |
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
│   │   ├── isr.asm           # ISR + SYSCALL entry 64-bit (SAVE_REGS 15 GPR)
│   │   ├── idt.h / idt.c     # IDT 64-bit (16-byte gate descriptor)
│   │   ├── tss.h / tss.c     # TSS64 + GDT descriptor 16-byte
│   │   ├── task.h / task.c   # Multitasking, context-switch, iretq frame
│   │   ├── vmm.h / vmm.c     # PMM bitmap (64MB) + VMM 4-level paging
│   │   ├── paging.h / paging.c  # Wrapper paging
│   │   ├── elf_loader.h / elf_loader.c  # ELF64 loader ke per-proses PML4
│   │   ├── memory.h / memory.c  # Heap kernel (malloc/free first-fit), 6MB
│   │   ├── kernel.c          # kernel_main(), shell loop, exception handler
│   │   ├── syscall.h / syscall.c  # Dispatch SYSCALL/SYSRET (55 syscall)
│   │   ├── shell.h / shell.c # Shell CLI interaktif (cd/pwd/env/export/\$VAR/&)
│   │   ├── vbe.h / vbe.c     # VBE mode setting + PCI BAR0 discovery
│   │   ├── graphics.h / graphics.c  # Framebuffer 32bpp primitif
│   │   ├── window.h / window.c      # Window manager
│   │   ├── taskbar.h / taskbar.c    # Taskbar
│   │   ├── keyboard.h / keyboard.c  # PS/2 keyboard + ring buffer
│   │   ├── mouse.h / mouse.c        # PS/2 mouse
│   │   ├── timer.h / timer.c        # PIT 1000Hz
│   │   ├── pic.h / pic.c            # 8259A PIC cascade
│   │   ├── apic.h / apic.c          # Local APIC (xAPIC) + IPI INIT/SIPI
│   │   ├── acpi.h / acpi.c          # Parser RSDP/RSDT/MADT (enumerasi CPU)
│   │   ├── smp.h / smp.c            # SMP bootstrap BSP/AP + status AP online
│   │   ├── smp_trampoline.asm       # AP trampoline real->protected->long mode
│   │   ├── spinlock.h               # Primitive spinlock atomic
│   │   ├── fs.h / fs.c              # Filesystem MFS3 (64×64KB, dirty/tmp/perms)
│   │   ├── ata.h / ata.c            # ATA PIO + Bus Master DMA driver
│   │   ├── ipc.h / ipc.c            # Message passing antar proses
│   │   ├── semaphore.h / semaphore.c
│   │   ├── pipe.h / pipe.c          # Anonymous pipe + named pipe
│   │   ├── shm.h / shm.c            # Shared memory (8 region × 4KB)
│   │   ├── rtl8139.h / rtl8139.c    # RTL8139 NIC driver (PCI, TX/RX polling)
│   │   ├── net.h / net.c            # Network stack: Ethernet + ARP + IPv4 + ICMP
│   │   ├── serial.h / serial.c      # COM1 debug output
│   │   ├── device.h / device.c      # Device framework
│   │   ├── drv_vga.c / drv_kbd.c    # VGA & keyboard device driver
│   │   └── *_elf_data.h      # Program user ter-embed sebagai C array
│   └── programs/
│       ├── lib.h             # Syscall wrapper + libc minimal (printf/sprintf/strcat/memcpy/...)
│       ├── user.ld           # Linker script user (ELF64, . = 0x400000)
│       ├── paint.c           # Aplikasi paint
│       ├── notepad.c         # Editor teks
│       ├── calc.c            # Kalkulator
│       ├── filemanager.c     # File manager GUI (MFS3)
│       ├── gui_term.c        # Terminal GUI
│       ├── clock.c           # Widget jam — uptime HH:MM:SS
│       ├── sysinfo.c         # Panel info sistem
│       └── ...               # Program demo lainnya (hello, gfxtest, gui_demo, sender, piper)
└── build/
    ├── os.img                # Disk image final (2MB, sektor raw)
    ├── disk.img              # Disk data sekunder (8MB, filesystem MFS3)
    └── kernel.bin            # Kernel binary (~223KB)
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

Membuka QEMU dengan `qemu-system-x86_64 -smp 2`, layar 1920×1080, GUI langsung muncul.
AP (Application Processor) akan boot otomatis — ketik `cpuinfo` di shell untuk verifikasi.

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
0x07000–0x07FFF  4KB     AP trampoline (real->pmode->lmode, di-copy smp_init)
0x08000–0x2FFFF ~160KB   Kernel binary (kernel_entry + kode C)
0x30000–0x8FFFF ~384KB   Stack BSP (tumbuh dari 0x90000 ke bawah)
0x9B000–0x9EFFF  16KB    Stack per-AP (8KB/AP: AP1=0x9D000, AP2=0x9B000)
0x100000–0x6FFFFF 6MB    Heap kernel (malloc/free)
0x400000–0x4FFFFF  1MB   User ELF load address (setiap proses)
0x500000–0x507FFF 32KB   Shared memory region (8 slot × 4KB, SHM)
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

User program memanggil syscall via instruksi `SYSCALL` (IA32_LSTAR MSR, ring-3 → ring-0):

```c
// lib.h — contoh penggunaan dari user space
draw_pixel(100, 200, GFX_RED);     // syscall SYS_DRAW_PIXEL
int w = win_create("Paint", ...);  // syscall SYS_WIN_CREATE
char c = getkey();                 // syscall SYS_GETKEY
void *buf = malloc(1024);          // syscall SYS_ALLOC
sprintf(buf, "tick=%d", ticks);    // lib.h printf/sprintf (F2)
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
| Tahap D — Kernel Stability & Driver | 1 Apr 2026 | SYSCALL/SYSRET, PMM, ATA DMA, Serial |
| Tahap E — Filesystem & I/O Lanjutan | 1 Apr 2026 | MFS3 subdirektori, dirty/tmpfs/perms/timestamp |
| Tahap F — Userspace & Shell Lanjutan | 1 Apr 2026 | Shell env/pipe/&, libc minimal, ELF64 loader |
| Tahap G — Networking | 1 Apr 2026 | RTL8139, ARP, IPv4, ICMP, ping/ifconfig |
| Tahap H — SMP Multi-core | 1 Apr 2026 | LAPIC, ACPI MADT, AP trampoline, `cpuinfo` |

---

## Lisensi

Proyek ini bersifat edukatif. Bebas digunakan untuk belajar dan referensi.
