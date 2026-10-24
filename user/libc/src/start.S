.section .text
.global _start
.type _start,@function
_start:
    xor %rbp,%rbp
    mov %rsp,%rdi
    andq $-16,%rsp
    call __libc_start
    ud2
.global __signal_restorer
.type __signal_restorer,@function
__signal_restorer:
    mov $15,%rax
    syscall
    ud2
.global syscall
.type syscall,@function
syscall:
    mov %rdi,%rax
    mov %rsi,%rdi
    mov %rdx,%rsi
    mov %rcx,%rdx
    mov %r8,%r10
    mov %r9,%r8
    mov 8(%rsp),%r9
    syscall
    cmp $-4095,%rax
    jae 1f
    ret
1:
    neg %eax
    mov %eax,errno(%rip)
    mov $-1,%rax
    ret
.section .note.GNU-stack,"",@progbits
