#include "internal.h"
#include "console/kprintf.h"
#include "proc/process.h"

struct descriptor_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap;
} __attribute__((packed));

struct idt_gate {
    uint16_t offset_low, selector;
    uint8_t ist, flags;
    uint16_t offset_mid;
    uint32_t offset_high, reserved;
} __attribute__((packed));

static uint64_t gdt[7] __attribute__((aligned(16)));
static struct tss tss;
static struct idt_gate idt[256] __attribute__((aligned(16)));
static uint8_t privilege_stack[32768] __attribute__((aligned(16)));
static uint8_t exception_stacks[3][16384] __attribute__((aligned(16)));
extern void *x64_isr_table[256];
extern void x64_load_gdt(const struct descriptor_pointer *pointer);

_Static_assert(sizeof(struct tss) == 104, "TSS layout");
_Static_assert(sizeof(struct idt_gate) == 16, "IDT gate layout");
_Static_assert(sizeof(struct x64_frame) == 176, "interrupt frame layout");

void x64_tables_init(void) {
    gdt[1] = 0x00af9a000000ffffull;
    gdt[2] = 0x00cf92000000ffffull;
    tss.rsp[0] = (uintptr_t)privilege_stack + sizeof(privilege_stack);
    for (unsigned i = 0; i < 3; i++)
        tss.ist[i] = (uintptr_t)exception_stacks[i] + sizeof(exception_stacks[i]);
    tss.iomap = sizeof(tss);
    uint64_t base = (uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    gdt[3] = 0x00cff2000000ffffull;
    gdt[4] = 0x00affa000000ffffull;
    gdt[5] = limit | ((base & 0xffffff) << 16) | (0x89ull << 40) | (((base >> 24) & 0xff) << 56);
    gdt[6] = base >> 32;
    struct descriptor_pointer gdtr = {sizeof(gdt) - 1, (uintptr_t)gdt};
    x64_load_gdt(&gdtr);
    for (unsigned i = 0; i < 256; i++) {
        uint64_t address = (uintptr_t)x64_isr_table[i];
        idt[i] = (struct idt_gate){.offset_low = address,
                                   .selector = 8,
                                   .flags = 0x8e,
                                   .offset_mid = address >> 16,
                                   .offset_high = address >> 32};
    }
    idt[3].flags = 0xee;
    idt[8].ist = 1;
    idt[2].ist = 2;
    idt[18].ist = 3;
    struct descriptor_pointer idtr = {sizeof(idt) - 1, (uintptr_t)idt};
    __asm__ volatile("lidt %0" : : "m"(idtr) : "memory");
}

struct x64_frame *x64_interrupt_dispatch(struct x64_frame *frame) {
    struct x64_frame *next = process_interrupt(frame);
    if (next)
        return next;
    if (frame->vector < 32) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        kprintf("\n[exception] vector=%llu error=0x%llx rip=0x%llx cs=0x%llx "
                "rflags=0x%llx rsp=0x%llx cr2=0x%llx\n",
                (unsigned long long)frame->vector, (unsigned long long)frame->error,
                (unsigned long long)frame->rip, (unsigned long long)frame->cs,
                (unsigned long long)frame->rflags, (unsigned long long)frame->rsp,
                (unsigned long long)cr2);
        hal_panic("unhandled CPU exception");
    }
    return frame;
}

void x64_set_tss_stack(uint64_t stack) {
    tss.rsp[0] = stack;
}
