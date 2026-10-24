#include "internal.h"
#include "memory.h"

static struct hal_boot_info memory;
static uint64_t page_tables[32][512] __attribute__((aligned(4096)));
static size_t tables_used;
static size_t mmio_used;
static uint64_t mmio_physical[32];
static const uint64_t mmio_base = 0xffffc00000000000ull;
static const uint64_t address_mask = 0x000ffffffffff000ull;

void x64_memory_init(const struct hal_boot_info *boot) {
    memory = *boot;
}

const void *x64_firmware_pointer(uint64_t physical, size_t length) {
    if (!length || physical > UINT64_MAX - length || memory.hhdm > UINT64_MAX - physical)
        return NULL;
    uint64_t end = physical + length;
    if (memory.hhdm > UINT64_MAX - (end - 1))
        return NULL;
    uint64_t cursor = physical;
    while (cursor < end) {
        uint64_t next = cursor;
        for (size_t i = 0; i < memory.region_count; i++) {
            const struct hal_memory_region *r = &memory.regions[i];
            if (r->mapped && r->length <= UINT64_MAX - r->base && cursor >= r->base &&
                cursor < r->base + r->length) {
                next = r->base + r->length;
                break;
            }
        }
        if (next == cursor)
            return NULL;
        cursor = next;
    }
    return (const void *)(uintptr_t)(memory.hhdm + physical);
}

static uint64_t new_table(void) {
    if (tables_used == 32)
        hal_panic("MMIO page table pool exhausted");
    uint64_t *table = page_tables[tables_used++];
    memset(table, 0, 4096);
    return (uintptr_t)table - memory.kernel_virtual + memory.kernel_physical;
}

volatile uint32_t *x64_map_mmio(uint64_t physical) {
    uint64_t page = physical & ~4095ull;
    if (page & ~address_mask)
        hal_panic("MMIO physical address outside x64 range");
    for (size_t i = 0; i < mmio_used; i++)
        if (mmio_physical[i] == page)
            return (volatile uint32_t *)(uintptr_t)(mmio_base + i * 4096 + (physical & 4095));
    if (mmio_used == 32)
        hal_panic("MMIO mapping pool exhausted");
    uint64_t virtual = mmio_base + mmio_used * 4096;
    uint64_t cr3, cr4;
    __asm__ volatile("mov %%cr3, %0; mov %%cr4, %1" : "=r"(cr3), "=r"(cr4));
    uint64_t table_phys = cr3 & address_mask;
    unsigned levels = (cr4 & (1u << 12)) ? 5 : 4;
    for (unsigned level = levels; level > 1; level--) {
        uint64_t *table = (uint64_t *)(uintptr_t)(memory.hhdm + table_phys);
        size_t index = (virtual >> (12 + 9 * (level - 1))) & 511;
        if (!(table[index] & 1))
            table[index] = new_table() | 3;
        if (table[index] & (1u << 7))
            hal_panic("MMIO virtual mapping overlaps huge page");
        table_phys = table[index] & address_mask;
    }
    uint64_t *table = (uint64_t *)(uintptr_t)(memory.hhdm + table_phys);
    size_t index = (virtual >> 12) & 511;
    if (table[index] & 1)
        hal_panic("MMIO virtual mapping already present");
    uint64_t flags = 0x1b;
    if (rdmsr(0xc0000080) & (1u << 11))
        flags |= 1ull << 63;
    table[index] = page | flags;
    __asm__ volatile("invlpg (%0)" : : "r"(virtual) : "memory");
    mmio_physical[mmio_used++] = page;
    return (volatile uint32_t *)(uintptr_t)(virtual + (physical & 4095));
}

const void *x64_rsdp(size_t length) {
    uint64_t address = (uintptr_t)memory.rsdp;
    if (address < memory.hhdm)
        return NULL;
    return x64_firmware_pointer(address - memory.hhdm, length);
}
