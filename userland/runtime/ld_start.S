.section .text, "ax"
.global _start
.type _start, @function
.extern ld_leonos_start
.extern _DYNAMIC
_start:
    xor %rbp, %rbp
    /* ld-leonos is itself a PIE.  Its dynamic section is deliberately small
     * and self-contained, so apply RELATIVE relocations before C touches the
     * GOT or writable global data.  The kernel supplies this image's load
     * bias in the dynamic launch record passed in r8. */
    mov 24(%r8), %r10
    lea _DYNAMIC(%rip), %r11
    xor %r12d, %r12d
    xor %r13d, %r13d
1:
    mov (%r11), %rax
    test %rax, %rax
    jz 2f
    cmp $7, %rax
    jne 3f
    mov 8(%r11), %r12
    jmp 3f
3:
    cmp $8, %rax
    jne 4f
    mov 8(%r11), %r13
4:
    add $16, %r11
    jmp 1b
2:
    test %r12, %r12
    jz 6f
    lea (%r10,%r12), %r11
    add %r11, %r13
5:
    cmp %r13, %r11
    jae 6f
    mov 8(%r11), %rax
    cmp $8, %eax
    jne 7f
    mov (%r11), %rdx
    add %r10, %rdx
    mov 16(%r11), %rax
    add %r10, %rax
    mov %rax, (%rdx)
7:
    add $24, %r11
    jmp 5b
6:
    mov %r8, %rcx
    /* argc, argv and envp retain the normal LeonOS launch registers. */
    call ld_leonos_start
    mov %eax, %edi
    mov $60, %eax
    int $0x80
1:
    hlt
    jmp 1b
