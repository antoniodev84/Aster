#include "internal.h"
#include "console/kprintf.h"

uint64_t hal_irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void hal_irq_restore(uint64_t state) {
    if (state & (1u << 9))
        __asm__ volatile("sti" : : : "memory");
    else
        __asm__ volatile("cli" : : : "memory");
}

void hal_interrupts_enable(void) {
    __asm__ volatile("sti" : : : "memory");
}
void hal_interrupts_disable(void) {
    __asm__ volatile("cli" : : : "memory");
}
void hal_idle(void) {
    __asm__ volatile("sti; hlt" : : : "memory");
}
uint8_t hal_io_read8(uint16_t port) {
    return in8(port);
}
void hal_io_write8(uint16_t port, uint8_t value) {
    out8(port, value);
}

void hal_debug_init(void) {
    out8(0x3f9, 0);
    out8(0x3fb, 0x80);
    out8(0x3f8, 1);
    out8(0x3f9, 0);
    out8(0x3fb, 3);
    out8(0x3fa, 0xc7);
    out8(0x3fc, 0x0b);
}

void hal_debug_putc(char c) {
    if (c == '\n')
        hal_debug_putc('\r');
    for (unsigned i = 0; i < 100000; i++) {
        if (in8(0x3fd) & 0x20) {
            out8(0x3f8, (uint8_t)c);
            return;
        }
        __asm__ volatile("pause");
    }
}

_Noreturn void hal_panic(const char *message) {
    hal_interrupts_disable();
    kprintf("\nPANIC: %s\n", message);
    for (;;)
        __asm__ volatile("hlt");
}

void hal_cpu_init(const struct hal_boot_info *boot) {
    hal_interrupts_disable();
    x64_tables_init();
    x64_memory_init(boot);
    kprintf("[hal] x86_64 BSP, GDT/TSS + IDT 256 vectors, IST DF/NMI/MC ready\n");
}

uint32_t hal_pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    if (slot > 31 || function > 7 || (offset & 3))
        return UINT32_MAX;
    uint64_t state = hal_irq_save();
    out32(0xcf8, 0x80000000u | (uint32_t)bus << 16 | (uint32_t)slot << 11 |
                     (uint32_t)function << 8 | offset);
    uint32_t value = in32(0xcfc);
    hal_irq_restore(state);
    return value;
}

void hal_pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset,
                            uint32_t value) {
    if (slot > 31 || function > 7 || (offset & 3))
        return;
    uint64_t state = hal_irq_save();
    out32(0xcf8, 0x80000000u | (uint32_t)bus << 16 | (uint32_t)slot << 11 |
                     (uint32_t)function << 8 | offset);
    out32(0xcfc, value);
    hal_irq_restore(state);
}
