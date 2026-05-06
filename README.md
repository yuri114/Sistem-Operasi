# Oria OS

Sistem operasi *from-scratch* berbasis x86_64 yang ditulis dalam Assembly (NASM) dan C, berjalan di **64-bit Long Mode**, dengan layar GUI 1920×1080 32bpp, multitasking preemptive, filesystem sendiri, dan window manager lengkap.

> Proyek ini adalah OS riil yang berjalan di QEMU — bukan emulasi, bukan simulator — dibangun sepenuhnya dari BIOS boot hingga GUI multi-window dengan jaringan, threading, dan user accounts.

---

## Tangkapan Layar

> *(jalankan `.\build.ps1 run` untuk melihat tampilan langsung)*

---

## Spesifikasi Teknis

| Komponen | Detail |
|---|---|
| Arsitektur | x86_64 — IA-32e Long Mode (64-bit) |
| Boot | MBR 512-byte → Protected Mode → Long Mode |
| Resolusi | 1920×1080 @ 32bpp (VBE Linear Framebuffer, ~8.3 MB) |
| Kernel | ~555 KB binary |
| Memory Map | 4-level paging, identity-mapped 4 GB, heap kernel 6 MB |
| Multitasking | Priority-based preemptive, hingga **32** task, PIT IRQ0 @ 1000 Hz |
| Ring | Kernel Ring-0 / User Ring-3 (isolasi penuh per-proses) |
| Filesystem | MFS3/MFS4 — 64 file × 64 KB, inode layer, symlink, hardlink |
| Syscall | `SYSCALL/SYSRET` (IA32_LSTAR MSR) — **109 syscall** tersedia |
| Networking | RTL8139, IPv4/IPv6, TCP/UDP, DHCP, DNS, HTTP, HTTPS/TLS 1.3 |
| Build | WSL + `x86_64-linux-gnu-gcc` + NASM, output `os.img` binary raw |
| Emulator | QEMU `qemu-system-x86_64` |

---

## Fitur

### Boot & Kernel Core

- **Boot 64-bit**: BIOS → Protected Mode → 4-level paging (PML4) → Long Mode
- **GDT 64-bit**: kernel CS/DS (ring-0), user CS/DS (ring-3), TSS64 (16-byte descriptor)
- **IDT 64-bit**: 16-byte gate descriptor, exception 0–14, IRQ 0/1/12
- **Heap kernel**: first-fit allocator + coalescing free, interrupt-safe, 6 MB (0x100000–0x6FFFFF)
- **PMM**: bitmap 16384 frame (64 MB), frame 0–767 pre-reserved untuk kernel
- **VMM 4-level**: `vmm_create_page_dir`, `vmm_map_page` (walk PML4→PDPT→PD→PT), `vmm_free_user_memory`
- **Demand paging**: `#PF` handler — page not-present di user space → `pmm_alloc_frame` + zero-fill + map, retry
- **Stack guard page**: zona 0x5FE000–0x5FEFFF tidak dipetakan → stack overflow → `task_exit()`
- **Serial debug**: COM1 output ke QEMU `-serial stdio`; kernel debugger: `dbg regs`, `dbg mem`, `dbg trace`, breakpoint

---

### Multitasking & Proses

- **Preemptive priority scheduler**: PIT IRQ0 (1000 Hz); skor = `priority×4 − nice + age_ticks÷8`; aging mencegah starvation
- **Nice value**: `renice <nice> <pid>` — nilai −20 (tinggi) s/d +19 (rendah); default 0
- **Context switch 64-bit**: 15 GPR (rax–r15) disimpan di stack, iretq frame; TSS64 rsp0 diperbarui setiap switch
- **Ring-3 isolation**: setiap proses punya PML4 sendiri + RSP user di 0x600000
- **ELF64 loader**: `vmm_map_page` per segment, load address 0x400000
- **fork() + Copy-on-Write**: `vmm_copy_cow()` tandai page RO+COW; write fault → alokasi frame baru + memcpy; `frame_cow_cnt` mencegah double-free
- **exec_replace()**: muat ELF baru ke PML4 baru, switch CR3, bebaskan PML4 lama, langsung iretq
- **SMP multi-core**: LAPIC INIT/SIPI, ACPI MADT parser, AP trampoline (real→protected→long mode), LAPIC timer per-AP 10 ms
- **Per-core scheduler**: BSP ambil task `is_user==1`; AP ambil `is_user==0` (atau work-steal)
- **Sinyal**: `SIGINT(2)`, `SIGTERM(15)`, `SIGKILL(9)` — `task_send_signal()`, `Ctrl+C` → SIGINT proses foreground
- **POSIX signal mask**: `sigprocmask(SIG_BLOCK/SIG_UNBLOCK/SIG_SETMASK, mask)` — blokir pengiriman sinyal per-proses; SIGKILL tidak bisa diblokir
- **Wait/exit**: `task_wait()`, `waitpid_ex()` — tunggu proses selesai, ambil exit code
- **Shell**: `ps`, `kill`, `exec`, `setprio`, `renice`, `cpuinfo`, `taskstat`

---

### Thread

- **Thread sejati**: berbagi `page_dir` dengan proses induk; stack isolasi per-thread di VA `0x700000 + tid×0x5000`
- **Stack 16 KB per-thread** + guard page pertama (→ `#PF` "THREAD STACK OVERFLOW")
- **Demand-paged thread stack**: tidak alokasi fisik di awal — tumbuh saat `#PF`
- **Thread join**: `task_wait(tid)` — tunggu thread selesai
- **Thread naming**: `SYS_THREAD_SET_NAME` — tampil di `ps` dengan label `[T:pid]`
- **Thread-local storage**: satu halaman TLS per thread di VA `0x800000 + tid×0x1000`; `FS_BASE` MSR diperbarui setiap context switch
- **Futex**: `futex_wait(addr, expected)` / `futex_wake(addr, n)` — mutex ringan tanpa syscall saat tidak ada kontestasi (CAS atomik)
- **Mutex user-space**: `mutex_lock/unlock` via CAS + `futex_wait/wake`; thread-safe malloc pakai mutex ini
- **Condition variable**: `cv_wait/signal/broadcast`, FIFO ring waiter (max 4), atomik release mutex saat wait
- **Semaphore kernel**: `SYS_SEM_ALLOC/WAIT/POST/FREE`, blocking sejati (bukan busy-wait), FIFO waiter (max 4)
- **Syscall**: `SYS_THREAD_CREATE(64)`, `SYS_THREAD_EXIT(65)`, `SYS_THREAD_JOIN(66)`, `SYS_THREAD_SET_NAME(67)`, `SYS_COND_*(68-72)`, `SYS_FUTEX_*(89-90)`, `SYS_GET_TLS(91)`

---

### Memory Management

- **`mmap(n_pages)`**: alokasi halaman anonim zero-fill, bump allocator mulai VA `0x900000`
- **`munmap(addr, n_pages)`**: `vmm_unmap_page` + `pmm_free_frame` per halaman
- **`mmap_file(fd, n_pages)`**: map isi file ke VA `0xB00000+`; baca file langsung ke frame fisik
- **`munmap_file(addr, n_pages)`**: bebaskan mapping file-backed
- **`SYS_BRK(62)`**: user heap `malloc` via bump allocator (0x400000+)
- **Shared memory**: `shm_create/attach/detach`, 8 region × 4 KB, VA `0x500000+slot×4096`
- **Meminfo**: `meminfo` — tampilkan total/used/free PMM frame
- **Resource limits (rlimit)**: batas memori heap (KB) dan jumlah fd per-proses; `task_set_rlimit()` / `task_get_rlimit()`; shell: `ulimit -v <KB>`, `ulimit -n <num>`

---

### Filesystem

- **MFS3**: custom raw filesystem, 64 file × 64 KB, pointer-based, dirty bit, tmpfs, permission R/W/X/DIR, timestamp ctime/mtime, ATA PIO
- **MFS4 inode layer**: 128 inode in-memory di atas MFS3; tipe FILE/DIR/SYMLINK; `mfs4_symlink`, `mfs4_hardlink`, `mfs4_stat`, `mfs4_listdir`, `mfs4_unlink`
- **Persist MFS4**: inode table flush ke disk (LBA 513–549), reload saat boot; `sync` menyimpan MFS3+MFS4 sekaligus
- **Rename**: `mfs4_rename(old, new)` — update inode + file MFS3 via `fs_rename()`
- **Subdirektori**: `mkdir`, `ls <dir>`, `cd`, `pwd`; akses `dir/file.txt`
- **EXT2 read-only**: baca superblock/inode/data dari ATA secondary; shell: `ext2ls`, `ext2read <path>`
- **VFS fd**: fd 0=stdin, 1=stdout, 2=stderr; `VFS_TYPE_FILE/PIPE/NET/TTY/STDIN/STDOUT`; syscall `SYS_OPEN(56)`, `SYS_READ_FD(57)`, `SYS_WRITE_FD(58)`, `SYS_CLOSE_FD(59)`
- **Non-blocking fd**: `VFS_O_NONBLOCK` — `vfs_read()` return `EAGAIN` jika tidak ada data; `SYS_FCNTL(93)`
- **poll()**: `SYS_POLL(94)` — `POLLIN/POLLOUT/POLLERR`, timeout ms; `vfs_fd_ready()` per tipe
- **Shell**: `ls`, `read`, `write`, `del`, `mkdir`, `chmod`, `rename`, `touch`, `cat`, `cp`, `mv`, `sync`, `open`, `fread`, `fwrite`, `fclose`

---

### IPC & Komunikasi Antar Proses

- **Message passing**: `SYS_MSG_SEND(7)` / `SYS_MSG_RECV(8)` — mailbox per-task, payload 56 byte
- **Message queue (MQ)**: `SYS_MQ_SEND(60)` / `SYS_MQ_RECV(61)` — per-task mailbox 8 slot
- **Pipe anonymous**: `SYS_PIPE2(77)` — 2 fd (read/write end), blocking, EOF saat writer tutup
- **Pipe named**: `SYS_PIPE_NAMED(52)` — nama + kernel ring buffer
- **Shell pipeline** `|`: `prog1 | prog2` — routing stdout prog1 ke stdin prog2 via VFS pipe; EOF propagation
- **Shell redirect**: `exec prog > file` (stdout ke file), `exec prog < file` (stdin dari file)
- **Shared memory**: `SYS_SHM_CREATE(53)`, `SYS_SHM_ATTACH(54)`, `SYS_SHM_DETACH(55)`

---

### Virtual Terminals

- **6 terminal virtual** (tty0–tty5): setiap VT menyimpan buffer teks 160×60, warna, dan posisi kursor sendiri
- **Ctrl+Alt+F1..F6**: beralih ke tty0–tty5 dari keyboard
- **`vt <n>`**: beralih terminal dari shell (n = 0–5)
- **Render on switch**: blit buffer teks VT baru ke layar secara langsung

---

### GUI & Window Manager

- **Graphics primitif**: `draw_pixel`, `fill_rect`, `draw_line`, font 8×16 pixel
- **Terminal konsol**: 160×60 karakter, font 8×16 (row-doubled), scroll via `g_textbuf`
- **Wallpaper**: dimuat dari `wallpaper.img` (960×540 BGRA32), di-scale 2× ke 1920×1080; hanya di GUI mode
- **Window Manager**: hingga 16 window simultan; drag, close, minimize/restore, resize (handle kanan-bawah)
- **Optimasi rendering**: partial redraw `wp_blit_region()` + `wm_redraw_region()` hanya area berubah; kursor 16×16 single-pass (save+outline+fill); skip `pixel_buf` saat drag
- **Taskbar**: quick-launch (Paint/Calc/Note/Files/Term), jam real-time HH:MM:SS
- **Desktop icons**: klik untuk buka aplikasi; overlap check saat redraw icon
- **Kursor mouse**: PS/2 protokol 3-byte; arrow cursor 16×16 dengan outline, erase langsung dari fb
- **Tema Catppuccin Mocha**: titlebar `#2D2D2D`, tombol close `#F38BA8`, teks `#CDD6F4`, flat style
- **Clipboard**: buffer kernel 512 byte; `Ctrl+Y` salin, `Ctrl+V` tempel; tersedia di editor & gui_term
- **Syscall GUI**: `SYS_WIN_CREATE`, `SYS_WIN_DRAW`, `SYS_WIN_EVENT`, `SYS_WIN_BTN_ADD`, `SYS_DRAW_PIXEL`, `SYS_FILL_RECT`, `SYS_DRAW_STR`, `SYS_MOUSE_GET`

---

### Networking

- **RTL8139 driver**: PCI scan (vendor 0x10EC/device 0x8139), I/O port, TX 4-descriptor round-robin, RX ring 8 KB (polling)
- **Ethernet + ARP**: frame 14-byte, ARP request/reply, cache 8 slot IP→MAC, balas ARP untuk IP kita
- **IPv4**: header 20 B, TTL=64, checksum one's complement; routing subnet / gateway
- **DHCP client**: Discover→Offer→Request→ACK; perbarui IP dan gateway otomatis; shell: `dhcp`
- **ICMP**: echo request/reply, RTT via `get_ticks()`; shell: `ping <ip>`
- **IPv6**: SLAAC EUI-64 → link-local `fe80::/10`; ICMPv6 echo; shell: `ping6 <addr>`
- **UDP**: `net_udp_send()`, tanpa koneksi; shell: `udp_send <ip> <port> <pesan>`
- **TCP stack**: 3-way handshake, PSH/ACK data, FIN/ACK teardown, RST detection
  - Retransmit timer (RTO 200 ms, max 5 retry → RST)
  - Out-of-order buffer (1 slot) + duplicate ACK
  - Keepalive (idle 30 s, 3 probe × 5 s → close)
  - 4 koneksi simultan (`TCP_MAX_CONN=4`)
  - TCP listen/accept server-side (`net_tcp_listen`, `net_tcp_accept`)
- **DNS resolver**: query A record ke 8.8.8.8:53 via UDP, 3 attempt × 1 s; shell: `nslookup <hostname>`
- **NTP client**: sinkronisasi waktu UTC dari `pool.ntp.org:123`; shell: `ntp`
- **HTTP client**: `GET` via HTTP/1.0; shell: `tcp_get <ip> <port>`
- **HTTPS/TLS 1.3**: TLS 1.3 client stub; shell: `https_get <url>`
- **HTTP server**: TCP listen port 8080; terima koneksi dari host Windows (`curl http://10.0.2.15:8080/`); shell: `httpd start`
- **WebSocket**: upgrade dari HTTP; shell: `wscat <url>`
- **VirtIO block**: driver VirtIO disk (MMIO), baca/tulis sektor; shell: `vblk_read`

---

### Shell & Utilitas

- **Shell interaktif** berbasis kernel thread; routing input keyboard → `shell_process_char()`
- **History**: 8 entri, navigasi ↑/↓
- **Tab-completion**: auto-complete perintah dan nama file
- **Environment variables**: `export KEY=VAL`, ekspansi `$VAR` di input
- **Arithmetic expansion**: `$(( expr ))` — evaluasi ekspresi integer (+, -, *, /, %, variabel `$VAR`, tanda kurung); contoh: `echo $((2+3*4))`
- **Operator**: `prog &` (background), `prog1 | prog2` (pipeline), `prog > file` / `prog < file` (redirect)
- **Text editor**: `edit <file>` — editor multi-baris penuh dengan keyboard; `Ctrl+S` simpan, `Ctrl+Y` copy, `Ctrl+V` paste
- **ACPI shutdown**: `shutdown` — kirim SCI event ke ACPI PM1a_CNT; OS mati bersih di QEMU
- **Utilitas file**: `ls`, `read`, `write`, `del`, `mkdir`, `chmod`, `rename`, `touch`, `cat`, `cp`, `mv`, `head <n>`, `wc`
- **Info sistem**: `ps` (tampilkan proses + CPU + thread), `meminfo`, `cpuinfo`, `taskstat`
- **Jaringan**: `ifconfig`, `ping`, `ping6`, `nslookup`, `dhcp`, `ntp`, `tcp_get`, `udp_send`, `httpd`
- **Proses**: `exec <prog>`, `kill <pid>`, `setprio <id> <prio>`, `renice <nice> <pid>`, `pipe <p1> <p2>`
- **Resource limits**: `ulimit` — tampilkan limit; `ulimit -v <KB>` set batas memori heap; `ulimit -n <num>` set batas fd
- **Terminal virtual**: `vt <n>` — beralih ke tty n (0–5)
- **Debugger kernel**: `dbg regs`, `dbg mem <addr> <len>`, `dbg trace`
- **GUI**: `gui` — masuk mode desktop GUI

---

### User Accounts & Keamanan

- **Database pengguna**: `/etc/passwd` format `username:hash32:uid:home\n`; di-persist ke disk MFS3
- **Hash password**: djb2-32 dari password; disimpan sebagai integer desimal
- **Login**: `login` — prompt username + password; verifikasi hash; set uid task saat ini
- **adduser**: `adduser <user> <pass> [uid]` — hanya root (uid=0)
- **passwd**: `passwd` — ubah password pengguna saat ini (update `/etc/passwd`)
- **su**: `su <user>` — pindah ke pengguna lain (root bisa ke siapa saja)
- **whoami**: tampilkan username dan uid task saat ini
- **uid per-task**: field `uid` di Task struct; diwariskan saat fork/exec
- **Ring-3 isolation**: setiap proses di PML4 terpisah; kernel tidak dapat diakses dari user space

### Scripting Shell

- **Shell script**: jalankan `.sh` — `sh <file>`, atau `exec <file>`
- **Kontrol alir**: `if/then/else/fi`, `for VAR in ...; do/done`, `while COND; do/done`
- **Test expression**: `[ -f FILE ]`, `[ -z VAR ]`, `[ A = B ]`, `[ A != B ]`
- **Variabel**: `VAR=val`, `$VAR`, `export VAR`
- **Arithmetic expansion**: `$(( expr ))` — integer dengan +−*/% dan variabel `$VAR`
- **Subshell capture**: `$(command)` — ganti dengan output perintah
- **Brace expansion**: `{a,b,c}` → `a b c`
- **Glob expansion**: `*.txt`, `file?.c` — cocokkan nama file
- **Here-doc**: `cmd <<EOF ... EOF`
- **Fungsi shell**: definisi `function nama { ... }` atau `nama() { ... }`; panggil dengan nama fungsi; mendukung rekursi dan pemanggilan dari body fungsi lain
- **set -e / set -x**: hentikan saat error / trace setiap command

---

| Aplikasi | Deskripsi |
|---|---|
| `paint` | Aplikasi gambar mouse — 16 warna, brush, eraser |
| `notepad` | Editor teks GUI dengan clipboard Ctrl+Y/V |
| `calc` | Kalkulator ekspresi dasar |
| `filemanager` | Browser file MFS3 GUI |
| `gui_term` | Terminal emulator dalam window GUI |
| `clock` | Widget jam — uptime HH:MM:SS, update setiap detik |
| `sysinfo` | Panel info sistem — PID, uptime, tick, arsitektur |
| `threadtest` | Demo threading: spawn 3 thread paralel, join, counter |
| `futextest` | Demo futex: 4 thread × 1000 iterasi via mutex; counter == 4000 |
| `polltest` | Demo poll(): non-blocking fd EAGAIN, POLLIN pipe dalam 2 s |
| `sigtest` | Demo sinyal: fork + SIGTERM + waitpid_ex → exit code 143 |
| `grep` | Filter baris stdin yang mengandung pola; dipakai di pipeline |
| `ls` | List file FS ke stdout; dipakai di pipeline `ls \| grep` |

---

### Libc Minimal (`lib.h`)

- **Variadic**: `va_list`, `va_start`, `va_arg`, `va_end` (GCC builtins)
- **Format**: `vsprintf`, `sprintf`, `printf` — `%d %i %u %x %X %s %c %p %%`, flags `- 0` width, modifier `l`
- **String**: `strcat`, `strstr`, `strtol`, `atoi`, `memcpy`, `memset`, `memmove`, `memcmp`
- **Thread API**: `thread_create/exit/join/set_name`, `mutex_lock/unlock`, `cond_wait/signal/broadcast`, `futex_wait/wake`, `get_tls()`
- **Process**: `fork()`, `exec_replace()`, `exit_code(n)`, `kill(tid, sig)`, `waitpid_ex(tid)`
- **Signals**: `sigprocmask_set(how, mask)` — blokir/buka blokir sinyal (SIG_BLOCK/UNBLOCK/SETMASK)
- **Resource limits**: `setrlimit_r(RLimit*)`, `getrlimit_r(RLimit*)` — set/get batas memori (KB) dan fd
- **Memory**: `mmap(n)`, `munmap(addr, n)`, `mmap_file(fd, n)`, `malloc`, `free`
- **VFS**: `sys_open`, `sys_read_fd`, `sys_write_fd`, `sys_close_fd`, `pipe2`, `fcntl_setfl`, `poll`
- **MFS4**: `mfs4_symlink_u`, `mfs4_hardlink_u`, `mfs4_stat_u`, `mfs4_listdir_u`, `mfs4_unlink_u`, `mfs4_rename_u`

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

Output: `build/os.img` (2 MB raw disk image), `build/disk.img` (8 MB MFS3 data), `build/wallpaper.img` (960×540 BGRA32)

### Jalankan

```powershell
.\build.ps1 run
```

Membuka QEMU dengan `-smp 2`, layar 1920×1080. Saat boot, OS masuk ke **mode console** (layar hitam, teks putih 160×60 karakter).

**Urutan boot:**
1. OS boot → mode console (shell teks, font 8×16)
2. Ketik `gui` → muat wallpaper, inisialisasi window manager, masuk desktop GUI
3. Klik ikon di taskbar/desktop untuk membuka aplikasi

**Pintasan keyboard:**
| Pintasan | Aksi |
|---|---|
| `gui` | Masuk mode GUI dari console |
| `Ctrl+Alt+F1..F6` | Pindah virtual terminal tty0–tty5 |
| `Ctrl+C` | Kirim SIGINT ke program foreground |
| `Ctrl+Y` | Salin teks ke clipboard (di editor) |
| `Ctrl+V` | Tempel dari clipboard |

### Clean

```powershell
.\build.ps1 clean
```

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
│   │   ├── kernel_entry.asm  # Setup 4-level paging + Long Mode [BITS 32→64]
│   │   ├── linker.ld         # Linker script (ELF64, . = 0x8000)
│   │   ├── isr.asm           # ISR + SYSCALL entry 64-bit (SAVE_REGS 15 GPR)
│   │   ├── kernel.c          # kernel_main(), shell loop, exception handler, virtual terminals
│   │   ├── syscall.h / .c    # Dispatch SYSCALL/SYSRET (99 syscall)
│   │   ├── shell.c           # Shell CLI interaktif + semua built-in commands
│   │   ├── task.h / .c       # Multitasking, priority+nice+aging, threading, context-switch
│   │   ├── vmm.h / .c        # PMM bitmap (64 MB) + VMM 4-level paging + COW
│   │   ├── memory.h / .c     # Heap kernel (malloc/free first-fit), 6 MB
│   │   ├── fs.h / .c         # Filesystem MFS3 (64×64 KB, dirty/tmp/perms/timestamp)
│   │   ├── mfs4.h / .c       # MFS4 inode layer (symlink, hardlink, stat, persist)
│   │   ├── ata.h / .c        # ATA PIO + Bus Master DMA driver
│   │   ├── vfs.h / .c        # VFS fd table per-task (FILE/PIPE/NET/TTY/STDIN/STDOUT)
│   │   ├── keyboard.h / .c   # PS/2 keyboard + ring buffer + keyboard_getchar_block
│   │   ├── mouse.h / .c      # PS/2 mouse (protokol 3-byte)
│   │   ├── graphics.h / .c   # Framebuffer 32bpp primitif (fill_rect optimized)
│   │   ├── window.h / .c     # Window manager (drag/resize/partial-redraw)
│   │   ├── wallpaper.h / .c  # Wallpaper loader + wp_blit_region optimized
│   │   ├── net.h / .c        # Network stack: Eth+ARP+IPv4+IPv6+TCP+UDP+DHCP+DNS+NTP
│   │   ├── rtl8139.h / .c    # RTL8139 NIC driver (PCI, TX/RX polling)
│   │   ├── acpi.h / .c       # ACPI RSDP/RSDT/MADT parser + shutdown
│   │   ├── smp.h / .c        # SMP: LAPIC, INIT/SIPI, AP trampoline, per-core scheduler
│   │   ├── semaphore.h / .c  # Semaphore FIFO multi-waiter (max 4 antrian)
│   │   ├── pipe.h / .c       # Anonymous + named pipe (blocking, EOF propagation)
│   │   ├── shm.h / .c        # Shared memory (8 region × 4 KB)
│   │   ├── condvar.h / .c    # Condition variable (cv_wait/signal/broadcast)
│   │   ├── serial.h / .c     # COM1 debug output + kernel debugger
│   │   └── *_elf_data.h      # Program user ter-embed sebagai C array
│   └── programs/
│       ├── lib.h             # Syscall wrapper + libc minimal + thread/mutex/cond/futex
│       ├── user.ld           # Linker script user (ELF64, . = 0x400000)
│       └── *.c               # Program user (paint, calc, notepad, filemanager, ...)
└── build/
    ├── os.img                # Disk image final (2 MB, sektor raw)
    ├── disk.img              # Disk data MFS3 (8 MB)
    ├── wallpaper.img         # Wallpaper raw BGRA32 (960×540, 4 MB)
    └── kernel.bin            # Kernel binary (~555 KB)
```

---

## Memory Map

```
Alamat Fisik    Ukuran    Isi
──────────────────────────────────────────────────────
0x00000–0x007FF   2 KB   Real Mode IVT + BDA
0x07C00–0x07DFF 512 B    Bootloader MBR
0x07000–0x07FFF   4 KB   AP trampoline (real→pmode→lmode)
0x08000–0x2FFFF ~160 KB  Kernel binary
0x30000–0x8FFFF ~384 KB  Stack BSP (tumbuh dari 0x90000 ke bawah)
0x9B000–0x9EFFF  16 KB   Stack per-AP (8 KB/AP)
0x100000–0x6FFFFF  6 MB  Heap kernel (malloc/free)
0x400000–0x5FDFFF ~1.9MB User heap (via SYS_BRK, per-proses)
0x500000–0x507FFF  32 KB Shared memory (8 slot × 4 KB)
0x5FE000–0x5FEFFF   4 KB Guard page (stack overflow → #PF)
0x5FF000–0x5FFFFF   4 KB User stack proses (demand-paged, RSP = 0x600000)
0x700000–0x70FFFF  64 KB Thread user stacks (0x700000 + tid×0x5000, 16 KB+guard)
0x800000–0x80FFFF  64 KB Thread-local storage (0x800000 + tid×0x1000)
0x900000+          ...   mmap anonim (bump allocator)
0xB00000+          ...   mmap file-backed (bump allocator)
```

---

## Arsitektur Sistem

```
+-----------------------------------------------------+
|  BIOS / SeaBIOS                                     |
|    INT 13h LBA -> load kernel ke 0x8000             |
+------------------+----------------------------------+
                   |
+------------------v----------------------------------+
|  boot.asm  [16-bit Real Mode]                       |
|    lgdt (GDT 32-bit flat) -> CR0.PE=1               |
+------------------+----------------------------------+
                   |
+------------------v----------------------------------+
|  kernel_entry.asm  [BITS 32 - Protected Mode]       |
|    Build PML4/PDPT/4xPD @ 0x1000-0x6000             |
|    CR4.PAE=1, CR3=0x1000, EFER.LME=1, CR0.PG=1     |
|    jmp 0x08:long_mode_entry                         |
+------------------+----------------------------------+
                   |
+------------------v----------------------------------+
|  kernel_entry.asm  [BITS 64 - Long Mode]            |
|    reload DS/ES/SS=0x10, RSP=0x90000                |
|    call kernel_main()                               |
+------------------+----------------------------------+
                   |
+------------------v----------------------------------+
|  kernel.c  kernel_main()                            |
|  paging -> vbe -> graphics -> vt_init -> shell      |
|  pmm -> vmm -> ata -> fs -> mfs4 -> ipc/sem/pipe    |
|  dev -> net -> smp -> timer -> pic -> idt -> task   |
|  sti -> console mode (g_gui_mode=0)                 |
|  keyboard loop:                                     |
|    g_gui_mode=0 -> shell_process_char()             |
|    g_gui_mode=1 -> wm_key_event()  [setelah `gui`] |
+-----------------------------------------------------+
```

---

## Syscall Interface

```
SYS_PRINT(0)       SYS_GETKEY(1)      SYS_EXIT(2)        SYS_ALLOC(3)
SYS_FREE(4)        SYS_FS_READ(5)     SYS_FS_WRITE(6)    SYS_MSG_SEND(7)
SYS_MSG_RECV(8)    SYS_KILL(9)        SYS_SEM_ALLOC(10)  SYS_SEM_FREE(11)
SYS_SEM_WAIT(12)   SYS_SEM_POST(13)   SYS_PIPE_*(14-18)  SYS_DEV_*(19-20)
SYS_DRAW_PIXEL(22) SYS_FILL_RECT(23)  SYS_DRAW_LINE(24)  SYS_DRAW_STR(25)
SYS_WIN_*(26-45)   SYS_MOUSE_GET(46)  SYS_GET_TICKS(47)  SYS_YIELD(48)
SYS_SLEEP(49)      SYS_EXEC(50)       SYS_FS_SYNC(49)    SYS_FS_MKDIR(51)
SYS_PIPE_NAMED(52) SYS_SHM_*(53-55)   SYS_OPEN(56)       SYS_READ_FD(57)
SYS_WRITE_FD(58)   SYS_CLOSE_FD(59)   SYS_MQ_SEND(60)    SYS_MQ_RECV(61)
SYS_BRK(62)        SYS_WAITPID(63)    SYS_THREAD_*(64-67) SYS_COND_*(68-72)
SYS_EXEC_REPLACE(74) SYS_MMAP(75)    SYS_MUNMAP(76)     SYS_PIPE2(77)
SYS_NET_OPEN(78)   SYS_TTY_OPEN(79)   SYS_PIPE_REDIRECT(80)
SYS_MFS4_*(81-86)  SYS_SIGACTION(87)  SYS_SIGKILL_SIG(88)
SYS_FUTEX_WAIT(89) SYS_FUTEX_WAKE(90) SYS_GET_TLS(91)    SYS_MFS4_RENAME(92)
SYS_FCNTL(93)      SYS_POLL(94)       SYS_GETARGV(95)
SYS_CLIP_COPY(96)  SYS_CLIP_PASTE(97) SYS_MMAP_FILE(98)  SYS_MUNMAP_FILE(99)
SYS_FORK(100)      SYS_NET_LISTEN(101) SYS_NET_ACCEPT(102) SYS_NET_UNLISTEN(103)
SYS_SETITIMER(104) SYS_GFX_FLIP(105)  SYS_TIME(106)
SYS_SIGPROCMASK(107) SYS_SETRLIMIT(108) SYS_GETRLIMIT(109)
```

---

## Milestone

| Tanggal | Fitur yang Ditambahkan |
|---|---|
| 24 Mar 2026 | Framebuffer 32bpp True Color |
| 26 Mar 2026 | Resolusi HD (1280×720) → Full HD (1920×1080) |
| 26 Mar 2026 | x86_64 Long Mode rewrite penuh |
| 1 Apr 2026 | SYSCALL/SYSRET, PMM, ATA DMA, Serial debug |
| 1 Apr 2026 | MFS3 filesystem (subdirektori, dirty, perms, timestamp) |
| 1 Apr 2026 | Shell interaktif (env/pipe/redirect/&), libc minimal |
| 1 Apr 2026 | RTL8139, Ethernet, ARP, IPv4, ICMP, ping |
| 1 Apr 2026 | SMP multi-core: LAPIC, ACPI MADT, AP trampoline |
| 1 Apr 2026 | Per-core scheduler, VFS fd, guard page, message queue |
| 1 Apr 2026 | TCP/UDP stack, HTTP GET terverifikasi |
| 2 Apr 2026 | User-space threading, TLS, futex, condition variable |
| 2 Apr 2026 | fork+COW, exec_replace, mmap/munmap |
| 2 Apr 2026 | MFS4 inode (symlink, hardlink, stat, persist ke disk) |
| 2 Apr 2026 | Shell pipeline, redirect, sinyal SIGINT/TERM/KILL |
| 3 Apr 2026 | Poll/non-blocking fd, TCP reliability, DNS resolver |
| 3 Apr 2026 | IPv6+ping6, TLS 1.3 stub, EXT2 read-only, kernel debugger |
| 3 Apr 2026 | Font 8×16, titlebar Catppuccin, resize handle, clipboard |
| 3 Apr 2026 | Rendering optimasi (partial redraw, kursor, drag, icon) |
| 3 Apr 2026 | DHCP client, virtual terminals tty0–tty5 |
| 3 Apr 2026 | Priority scheduler + nice + aging (starvation prevention) |
| 3 Apr 2026 | File-backed mmap (mmap_file/munmap_file) |
| 3 Apr 2026 | User accounts + login (adduser, passwd, su, whoami) |
| 7 Mei 2026 | **Tier 1**: POSIX signal mask (`sigprocmask` SYS 107) |
| 7 Mei 2026 | **Tier 1**: Arithmetic expansion `$(( expr ))` di shell |
| 7 Mei 2026 | **Tier 1**: ulimit / rlimit per-proses (SYS 108-109) |
| 7 Mei 2026 | **Tier 1**: Fungsi shell `function name { }` dalam script |

---

## Lisensi

Proyek ini bersifat edukatif. Bebas digunakan untuk belajar dan referensi.
