/* ld.lld only creates PT_INTERP from an input .interp section.  Keep the
 * interpreter path with the dynamic CRT so every PIE has a loader contract
 * even when linked through our custom linker script. */
.section .interp, "a"
.asciz "0:/system/lib/ld-leonos.elf"

.section .text, "ax"
.global _start
.type _start, @function
.extern environ
.extern leonos_dynamic_exit
_start:
    xor %rbp, %rbp
    /* The kernel and ld-leonos preserve the established LeonOS launch ABI. */
    mov %rdi, %r12
    mov %rsi, %r13
    /* The runtime loader may tail-transfer here after finishing relocation.
     * Unlike a normal call, that transfer does not push a return address, so
     * normalize the caller boundary before invoking compiled application code.
     * System V AMD64 requires main to enter with rsp % 16 == 8. */
    and $-16, %rsp
    /* `environ` lives in libleonos.  Access it through the PIE GOT so the
     * executable never emits the unsupported R_X86_64_COPY relocation. */
    test %rdx, %rdx
    jne 1f
    lea leonos_empty_environment(%rip), %rdx
1:
    mov %rdx, %r14
    mov environ@GOTPCREL(%rip), %rax
    mov %rdx, (%rax)
    /* ld-leonos has already run the main object's preinit/init arrays after
     * dependency initialization.  Picolibc's __libc_init_array belongs to
     * libleonos itself, so calling it here would run runtime constructors a
     * second time rather than the executable's constructors. */
    mov %r12, %rdi
    mov %r13, %rsi
    mov %r14, %rdx
    call main
    mov %eax, %edi
    call leonos_dynamic_exit

.section .bss, "aw", @nobits
.p2align 3
leonos_empty_environment:
    .quad 0
