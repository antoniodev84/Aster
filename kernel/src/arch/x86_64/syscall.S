.section .text
.global x64_syscall_entry
.type x64_syscall_entry,@function
x64_syscall_entry:
    swapgs
    movq %rsp,%gs:8
    movq %gs:0,%rsp
    pushq $0x1b
    pushq %gs:8
    pushq %r11
    pushq $0x23
    pushq %rcx
    pushq $0
    pushq $256
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    cld
    movq %rsp,%rdi
    andq $-16,%rsp
    call syscall_dispatch
    movq %rax,%rsp
    jmp x64_restore_frame
.global x64_enter_frame
.type x64_enter_frame,@function
x64_enter_frame:
    movq %rdi,%rsp
    jmp x64_restore_frame
.section .note.GNU-stack,"",@progbits
