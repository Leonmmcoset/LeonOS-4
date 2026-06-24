.section .text, "ax"
.global _start
.type _start, @function
_start:
    xor %rbp, %rbp
    mov %rsp, %rdi
    call main
    mov %eax, %edi
    call exit
