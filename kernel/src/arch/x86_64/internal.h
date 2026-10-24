#ifndef X64_INTERNAL_H
#define X64_INTERNAL_H

#include "hal/hal.h"

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

static inline void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

struct x64_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector, error, rip, cs, rflags, rsp, ss;
};

struct acpi_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem[6];
    char table_id[8];
    uint32_t oem_revision;
    uint32_t creator;
    uint32_t creator_revision;
} __attribute__((packed));

struct x64_ioapic_info {
    uint32_t address, gsi_base;
};

struct x64_iso {
    uint32_t gsi;
    uint16_t flags;
    bool present;
};

struct x64_nmi {
    uint32_t uid;
    uint8_t lint;
    uint16_t flags;
};

struct x64_nmi_source {
    uint32_t gsi;
    uint16_t flags;
};

struct x64_acpi_info {
    uint64_t lapic_address;
    struct x64_ioapic_info ioapics[8];
    size_t ioapic_count;
    struct x64_iso isa[16];
    struct x64_nmi nmis[32];
    size_t nmi_count;
    struct x64_nmi_source sources[16];
    size_t source_count;
    uint32_t cpu_count;
    uint32_t bsp_uid;
    bool bsp_found;
};

void x64_memory_init(const struct hal_boot_info *boot);
const void *x64_rsdp(size_t length);
const void *x64_firmware_pointer(uint64_t physical, size_t length);
volatile uint32_t *x64_map_mmio(uint64_t physical);
void x64_tables_init(void);
bool x64_acpi_init(struct x64_acpi_info *info, uint32_t bsp_id);
bool x64_acpi_lapic_address(uint64_t *address);
struct x64_frame *x64_interrupt_dispatch(struct x64_frame *frame);
void x64_apic_dispatch(uint8_t vector);
uint32_t x64_lapic_read(uint32_t offset);
void x64_lapic_write(uint32_t offset, uint32_t value);
bool x64_apic_ready(void);
void x64_timer_interrupt(void);

#endif
