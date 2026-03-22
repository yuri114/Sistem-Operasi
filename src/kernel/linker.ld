/* linker.ld - Linker Script untuk Kernel MyOS */
/* Kernel dimuat di alamat 0x1000 oleh bootloader */

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

    .bss :
    {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
}
