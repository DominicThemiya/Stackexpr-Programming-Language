/* Linker script for kernels that use asm<multiboot>. Loads at 1 MiB. */

ENTRY(_sx_mb_entry)

SECTIONS
{
    . = 0x100000;
    _sx_load_start = .;

    .multiboot : { KEEP(*(.multiboot)) }
    .text      : ALIGN(16) { *(.text .text.*) }
    .rodata    : ALIGN(16) { *(.rodata .rodata.*) }
    .data      : ALIGN(16) { *(.data .data.*) }
    _sx_load_end = .;

    .bss       : ALIGN(4K) { *(.bss .bss.*) *(COMMON) }
    _sx_bss_end = .;

    /DISCARD/ : { *(.note*) *(.comment) }
}
