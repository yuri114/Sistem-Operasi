OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
SECTIONS {
    . = 0xC00000;
    .text : { *(.text) }
    .data : { *(.data) }
    .bss  : { *(.bss) }
}