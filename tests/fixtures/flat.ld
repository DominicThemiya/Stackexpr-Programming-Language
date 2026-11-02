ENTRY(kmain)
/* Minimal kernel layout without a boot stub, used to check freestanding output. */

SECTIONS
{
    . = 0x100000;

    .text   : ALIGN(4K) { *(.text .text.*) }
    .rodata : ALIGN(4K) { *(.rodata .rodata.*) }
    .data   : ALIGN(4K) { *(.data .data.*) }
    .bss    : ALIGN(4K) { *(.bss .bss.*) *(COMMON) }
}
