ENTRY(_start)
PHDRS
{
    headers PT_PHDR PHDRS;
    text PT_LOAD FILEHDR PHDRS FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC FLAGS(4);
    note PT_NOTE FLAGS(4);
}
SECTIONS
{
    . = SIZEOF_HEADERS;
    .note.leonos : { KEEP(*(.note.leonos)) } :note
    .text ALIGN(4K) : { *(.text .text.*) } :text
    .rodata ALIGN(4K) : { *(.rodata .rodata.*) } :rodata
    /* The bootstrap applies this PIE's RELATIVE relocations before any C
     * code runs.  Keep every relocation target, including GOT and dynamic
     * bookkeeping, in a writable PT_LOAD page. */
    .dynamic ALIGN(4K) : { *(.dynamic) } :data :dynamic
    .got ALIGN(8) : { *(.got .got.*) } :data
    .preinit_array ALIGN(8) : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array .preinit_array.*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } :data
    .init_array ALIGN(8) : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .init_array.* .ctors .ctors.*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } :data
    .fini_array ALIGN(8) : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array .fini_array.* .dtors .dtors.*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } :data
    .data ALIGN(4K) : { *(.data .data.*) . = ALIGN(4K); } :data
    .bss : { *(COMMON) *(.bss .bss.*) } :data
    /DISCARD/ : { *(.eh_frame) *(.comment) }
}
