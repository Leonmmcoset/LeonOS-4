.section .text, "ax"
.global _start
.type _start, @function
.extern environ
_start:
    xor %rbp, %rbp
    /* __libc_init_array may overwrite caller-saved launch registers. */
    mov %rdi, %r12
    mov %rsi, %r13
    /* A NULL envp is a valid empty environment in the LeonOS launch ABI,
     * but libc users require environ to point at a NULL-terminated vector. */
    test %rdx, %rdx
    jne 1f
    lea leonos_empty_environment(%rip), %rdx
1:
    mov %rdx, %r14
    mov %rdx, environ(%rip)
    call __libc_init_array
    mov %r12, %rdi
    mov %r13, %rsi
    mov %r14, %rdx
    call main
    mov %eax, %edi
    call exit

.section .bss, "aw", @nobits
.p2align 3
leonos_empty_environment:
    .quad 0
