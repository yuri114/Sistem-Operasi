/* linker.ld - Linker Script untuk Kernel MyOS */
/* Kernel dimuat di alamat 0x1000 oleh bootloader */

ENTRY(kernel_main)

SECTIONS
{
    . = 0x1000;

    .text :
    {
        *(.text)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(.bss)
    }
}
