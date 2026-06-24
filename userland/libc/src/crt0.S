.section .text, "ax"
.global _start
.type _start, @function
_start:
    xor %rbp, %rbp
    call main
    mov %eax, %edi
    call exit
