.section .text
.global _start
_start:
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi
    and $-16, %rsp
    call main
    mov %eax, %edi
    mov $60, %eax
    syscall
    ud2
.global ncc_aster_syscall
ncc_aster_syscall:
    mov %rdi, %rax
    mov %rsi, %rdi
    mov %rdx, %rsi
    mov %rcx, %rdx
    mov %r8, %r10
    mov %r9, %r8
    mov 8(%rsp), %r9
    syscall
    ret
.section .note.GNU-stack,"",@progbits
