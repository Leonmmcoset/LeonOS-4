ENTRY(_start)
PHDRS
{
    headers PT_PHDR PHDRS;
    text PT_LOAD FILEHDR PHDRS FLAGS(5);
    interp PT_INTERP FLAGS(4);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC FLAGS(4);
    note PT_NOTE FLAGS(4);
    relro PT_GNU_RELRO FLAGS(4);
}
SECTIONS
{
    . = SIZEOF_HEADERS;
    .interp : { *(.interp) } :text :interp
    .note.leonos : { KEEP(*(.note.leonos)) } :note
    .text ALIGN(4K) : { *(.text .text.*) } :text
    .rodata ALIGN(4K) : { *(.rodata .rodata.*) } :rodata
    .dynsym : { *(.dynsym) } :rodata
    .dynstr : { *(.dynstr) } :rodata
    .hash : { *(.hash) } :rodata
    .rela.dyn : { *(.rela.dyn) } :rodata
    .rela.plt : { *(.rela.plt) } :rodata
    .dynamic : { *(.dynamic) } :rodata :dynamic
    /* The kernel validates PT_LOAD ranges at page granularity.  Keep mutable
     * linker state off the last read-only page, then let PT_GNU_RELRO lock it
     * after eager relocation. */
    .got ALIGN(4K) : { *(.got .got.*) } :data :relro
    .preinit_array ALIGN(8) : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array .preinit_array.*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } :data :relro
    .init_array ALIGN(8) : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    } :data :relro
    .fini_array ALIGN(8) : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.fini_array .fini_array.* .dtors .dtors.*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } :data :relro
    /* Emit file-backed data through a page boundary.  The remaining NOBITS
     * range is then a separate zero-filled mapping in ld-leonos. */
    .data ALIGN(4K) : { *(.data .data.*) . = ALIGN(4K); } :data
    .bss : { *(COMMON) *(.bss .bss.*) } :data
    /DISCARD/ : { *(.eh_frame) *(.comment) }
}
