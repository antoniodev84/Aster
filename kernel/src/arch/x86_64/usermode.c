#include "internal.h"

struct cpu_local {
    uint64_t stack, user_stack;
};
static struct cpu_local local;
extern void x64_syscall_entry(void);
extern void x64_set_tss_stack(uint64_t stack);
void x64_set_kernel_stack(uint64_t stack) {
    local.stack = stack;
    x64_set_tss_stack(stack);
}
void x64_usermode_init(void) {
    wrmsr(0xc0000101, (uintptr_t)&local);
    wrmsr(0xc0000102, 0);
    wrmsr(0xc0000080, rdmsr(0xc0000080) | 1);
    wrmsr(0xc0000081, (0x10ull << 48) | (8ull << 32));
    wrmsr(0xc0000082, (uintptr_t)x64_syscall_entry);
    wrmsr(0xc0000084, (1u << 8) | (1u << 9) | (1u << 10) | (1u << 12) | (1u << 13) | (1u << 14) |
                          (1u << 16) | (1u << 17) | (1u << 18));
}
