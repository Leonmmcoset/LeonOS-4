#ifdef LEONOS_KERNEL_LEGACY_MULTIBOOT
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
    .long 1024
    .long 768
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
boot_stack_bottom:
    .skip 65536
boot_stack_top:

.section .text, "ax"
.global _start
.type _start, @function
_start:
    mov %eax, %r12d
    mov %ebx, %r13d
    cli
    mov $boot_stack_top, %rsp
    and $-16, %rsp
    xor %rbp, %rbp
    mov %r12d, %edi
    mov %r13d, %esi
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt
#endif

.global x86_64_outb
.type x86_64_outb, @function
x86_64_outb:
    mov %dil, %al
    mov %si, %dx
    out %al, %dx
    ret

.global x86_64_inb
.type x86_64_inb, @function
x86_64_inb:
    mov %di, %dx
    in %dx, %al
    ret

.global x86_64_outw
.type x86_64_outw, @function
x86_64_outw:
    mov %di, %ax
    mov %si, %dx
    out %ax, %dx
    ret

.global x86_64_outl
.type x86_64_outl, @function
x86_64_outl:
    mov %edi, %eax
    mov %si, %dx
    out %eax, %dx
    ret

.global x86_64_inl
.type x86_64_inl, @function
x86_64_inl:
    mov %di, %dx
    in %dx, %eax
    ret

.global x86_64_halt
.type x86_64_halt, @function
x86_64_halt:
    cli
1:
    hlt
    jmp 1b

.global x86_64_lgdt
.type x86_64_lgdt, @function
x86_64_lgdt:
    lgdt (%rdi)
    ret

.global x86_64_lidt
.type x86_64_lidt, @function
x86_64_lidt:
    lidt (%rdi)
    ret

.global x86_64_ltr
.type x86_64_ltr, @function
x86_64_ltr:
    ltr %di
    ret

.global x86_64_load_cr3
.type x86_64_load_cr3, @function
x86_64_load_cr3:
    mov %rdi, %cr3
    ret

.global x86_64_read_cr2
.type x86_64_read_cr2, @function
x86_64_read_cr2:
    mov %cr2, %rax
    ret

.global x86_64_invlpg
.type x86_64_invlpg, @function
x86_64_invlpg:
    invlpg (%rdi)
    ret

.global x86_64_load_segments
.type x86_64_load_segments, @function
x86_64_load_segments:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    ret

.global arch_enter_user
.type arch_enter_user, @function
arch_enter_user:
    mov %rdi, %r11
    mov %rsi, %rsp
    pushq $0x1b
    pushq %rsi
    pushfq
    popq %rax
    orq $0x200, %rax
    pushq %rax
    pushq $0x23
    pushq %r11
    iretq

.global arch_enter_user_frame
.type arch_enter_user_frame, @function
arch_enter_user_frame:
    mov %rsi, %cr3
    mov %rdi, %rsp
    jmp trap_restore_frame

.macro ISR_NOERR name vector
.global \name
.type \name, @function
\name:
    mov $\vector, %rdi
    mov $0, %rsi
    mov 0(%rsp), %rdx
    mov 8(%rsp), %rcx
    mov 16(%rsp), %r8
    mov $0, %r9
    sub $8, %rsp
    movq $0, (%rsp)
    test $3, %cl
    jz 2f
    mov 24+8(%rsp), %r9
    mov 32+8(%rsp), %rax
    mov %rax, (%rsp)
2:
    call exception_dispatch
1:
    hlt
    jmp 1b
.endm

.macro ISR_ERR name vector
.global \name
.type \name, @function
\name:
    mov $\vector, %rdi
    mov 0(%rsp), %rsi
    mov 8(%rsp), %rdx
    mov 16(%rsp), %rcx
    mov 24(%rsp), %r8
    mov $0, %r9
    sub $8, %rsp
    movq $0, (%rsp)
    test $3, %cl
    jz 2f
    mov 32+8(%rsp), %r9
    mov 40+8(%rsp), %rax
    mov %rax, (%rsp)
2:
    call exception_dispatch
1:
    hlt
    jmp 1b
.endm

ISR_NOERR isr0_stub 0
ISR_NOERR isr1_stub 1
ISR_NOERR isr2_stub 2
ISR_NOERR isr3_stub 3
ISR_NOERR isr4_stub 4
ISR_NOERR isr5_stub 5
ISR_NOERR isr6_stub 6
ISR_NOERR isr7_stub 7
ISR_ERR isr8_stub 8
ISR_NOERR isr9_stub 9
ISR_ERR isr10_stub 10
ISR_ERR isr11_stub 11
ISR_ERR isr12_stub 12
ISR_ERR isr13_stub 13
ISR_NOERR isr15_stub 15
ISR_NOERR isr16_stub 16
ISR_ERR isr17_stub 17
ISR_NOERR isr18_stub 18
ISR_NOERR isr19_stub 19
ISR_NOERR isr20_stub 20
ISR_ERR isr21_stub 21
ISR_NOERR isr22_stub 22
ISR_NOERR isr23_stub 23
ISR_NOERR isr24_stub 24
ISR_NOERR isr25_stub 25
ISR_NOERR isr26_stub 26
ISR_NOERR isr27_stub 27
ISR_NOERR isr28_stub 28
ISR_ERR isr29_stub 29
ISR_ERR isr30_stub 30
ISR_NOERR isr31_stub 31

.global isr14_stub
.type isr14_stub, @function
isr14_stub:
    pushq $14
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    mov %rsp, %rdi
    call page_fault_dispatch
    jmp trap_restore_frame

.macro PUSH_TRAP_FRAME vector error
    pushq $\error
    pushq $\vector
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
.endm

.macro IRQ_STUB name vector
.global \name
.type \name, @function
\name:
    PUSH_TRAP_FRAME \vector 0
    mov %rsp, %rdi
    call irq_dispatch
    test %rax, %rax
    jz 1f
    mov %rax, %r12
    mov %rax, %rdi
    call sched_task_cr3
    mov %rax, %r13
    mov %r12, %rdi
    call sched_task_frame
    mov %rax, %rdi
    mov %r13, %rsi
    test %rdi, %rdi
    jz 1f
    mov %rsi, %cr3
    mov %rdi, %rsp
    jmp trap_restore_frame
1:
    jmp trap_restore_frame
.endm

IRQ_STUB irq0_stub 0x20
IRQ_STUB irq1_stub 0x21
IRQ_STUB irq12_stub 0x2c
IRQ_STUB irq32_stub 0x40

.global isr80_stub
.type isr80_stub, @function
isr80_stub:
    PUSH_TRAP_FRAME 0x80 0
    mov %rsp, %rdi
    call int80_dispatch
    test %rax, %rax
    jz 1f
    mov %rax, %r12
    mov %rax, %rdi
    call sched_task_cr3
    mov %rax, %r13
    mov %r12, %rdi
    call sched_task_frame
    mov %rax, %rdi
    mov %r13, %rsi
    test %rdi, %rdi
    jz 1f
    mov %rsi, %cr3
    mov %rdi, %rsp
    jmp trap_restore_frame
1:
    jmp trap_restore_frame

.global trap_restore_frame
.type trap_restore_frame, @function
trap_restore_frame:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq
