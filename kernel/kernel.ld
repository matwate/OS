OUTPUT_FORMAT(binary)
ENTRY(kernel_main)

SECTIONS {
    . = 0x7E00;
    .text.start : { *(.text.start) }
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
