/*
 * LeonOS osmlayer linker script: fixes the load address and image sections.
 * Produces the relocatable middle-layer module consumed by the kernel.
 */
ENTRY(osmlayer_module_init)

SECTIONS
{
    . = 144M;

    .text ALIGN(4K) : {
        *(.text .text.*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) : {
        *(.data .data.*)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
        *(.comment)
    }
}
