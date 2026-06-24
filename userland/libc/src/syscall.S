.section .text, "ax"
.global syscall0
.type syscall0, @function
syscall0:
    mov %rdi, %rax
    int $0x80
    ret

.global syscall1
.type syscall1, @function
syscall1:
    mov %rdi, %rax
    mov %rsi, %rdi
    int $0x80
    ret

.global syscall2
.type syscall2, @function
syscall2:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    int $0x80
    ret

.global syscall3
.type syscall3, @function
syscall3:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    int $0x80
    ret

.global syscall6
.type syscall6, @function
syscall6:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8, %r10
    mov %r9, %r8
    mov 8(%rsp), %r9
    int $0x80
    ret
