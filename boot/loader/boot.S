.section .multiboot, "a"
.align 8
multiboot2_header:
    .long 0xe85250d6
    .long 0
    .long multiboot2_header_end - multiboot2_header
    .long -(0xe85250d6 + 0 + (multiboot2_header_end - multiboot2_header))

    .align 8
    .short 1
    .short 0
    .long 20
    .long 4
    .long 6
    .long 17

    .align 8
    .short 5
    .short 0
    .long 20
    .long 0
    .long 0
    .long 0
    .long 32

    .align 8
    .short 7
    .short 0
    .long 8

    .align 8
    .short 9
    .short 0
    .long 12
    .long _start

    .align 8
    .short 0
    .short 0
    .long 8
multiboot2_header_end:

.section .bss, "aw", @nobits
.align 16
loader_stack_bottom:
    .skip 65536
loader_stack_top:

.section .text, "ax"
.global _start
.type _start, @function
_start:
    mov %eax, %edi
    mov %ebx, %esi
    cli
    mov $loader_stack_top, %rsp
    and $-16, %rsp
    xor %rbp, %rbp
    call loader_main

.halt:
    cli
    hlt
    jmp .halt

.global loader_outb
.type loader_outb, @function
loader_outb:
    mov %dil, %al
    mov %si, %dx
    out %al, %dx
    ret

.global loader_inb
.type loader_inb, @function
loader_inb:
    mov %di, %dx
    in %dx, %al
    ret
