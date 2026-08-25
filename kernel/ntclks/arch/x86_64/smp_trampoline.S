/* Real-mode -> protected-mode -> long-mode AP entry copied to low memory. */
.section .text.smp_trampoline, "ax"
.global smp_trampoline_start
.global smp_trampoline_end
.global smp_trampoline_cr3_imm
.global smp_trampoline_stack_imm
.global smp_trampoline_cpu_imm
.global smp_trampoline_entry_imm
.global smp_trampoline_gdtr_base
.global smp_trampoline_gdtr
.global smp_trampoline_gdt
.global smp_trampoline_pmode_off
.global smp_trampoline_longmode_off
.global smp_trampoline_pmode
.global smp_trampoline_longmode
.code16
smp_trampoline_start:
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    .byte 0x0f, 0x01, 0x16
smp_trampoline_gdtr_base:
    .word 0
    .byte 0x66, 0x0f, 0x20, 0xc0
    or $1, %eax
    .byte 0x66, 0x0f, 0x22, 0xc0
    .byte 0xea
smp_trampoline_pmode_off:
    .word 0
    .word 0x08

.code32
smp_trampoline_pmode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4
    .byte 0xb8
smp_trampoline_cr3_imm:
    .long 0
    mov %eax, %cr3
    mov $0xc0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr
    mov %cr0, %eax
    or $(1 << 31), %eax
    mov %eax, %cr0
    .byte 0xea
smp_trampoline_longmode_off:
    .long 0
    .word 0x18

.code64
smp_trampoline_longmode:
    mov $0x20, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    .byte 0x48, 0xbc
smp_trampoline_stack_imm:
    .quad 0
    .byte 0xbf
smp_trampoline_cpu_imm:
    .long 0
    .byte 0x48, 0xb8
smp_trampoline_entry_imm:
    .quad 0
    call *%rax
1:
    cli
    hlt
    jmp 1b

.align 8
smp_trampoline_gdt:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
smp_trampoline_gdt_end:
smp_trampoline_gdtr:
    .word smp_trampoline_gdt_end - smp_trampoline_gdt - 1
    .long 0
smp_trampoline_end:
