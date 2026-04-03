/* linker.ld — Script linker 64-bit untuk Oria OS */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(kernel_main)

SECTIONS
{
    . = 0x8000;

    .text :
    {
        *(.text)
    }

    .rodata :
    {
        *(.rodata)
    }

    .data :
    {
        *(.data)
    }

    /* BSS dipindah ke 0x4000000 (64MB) — di luar jangkauan PMM sepenuhnya.
     * PMM mengelola frame 0–16383 (0x0–0x3FFFFFFF); frame 16384 = 0x4000000
     * tidak pernah dikembalikan oleh pmm_alloc_frame() sehingga BSS aman.
     * Kernel PD[0-1GB][32] memetakan VA=PA=0x4000000 (2MB large page). */
    . = 0x4000000;
    .bss :
    {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
}
