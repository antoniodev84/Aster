#include "internal.h"
#include "memory.h"
#include "console/kprintf.h"

struct rsdp {
    char signature[8];
    uint8_t checksum;
    char oem[6];
    uint8_t revision;
    uint32_t rsdt;
    uint32_t length;
    uint64_t xsdt;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

static const struct acpi_header *madt;

static bool checksum(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint8_t sum = 0;
    for (size_t i = 0; i < length; i++)
        sum += bytes[i];
    return sum == 0;
}

static uint32_t u32(const void *data) {
    uint32_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}

static uint64_t u64(const void *data) {
    uint64_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}

static uint16_t u16(const void *data) {
    uint16_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}

static const struct acpi_header *table(uint64_t physical) {
    const struct acpi_header *header = x64_firmware_pointer(physical, sizeof(*header));
    if (!header || header->length < sizeof(*header) || header->length > 1024 * 1024)
        return NULL;
    header = x64_firmware_pointer(physical, header->length);
    return header && checksum(header, header->length) ? header : NULL;
}

static bool find_madt(void) {
    if (madt)
        return true;
    const struct rsdp *rsdp = x64_rsdp(20);
    if (!rsdp || memcmp(rsdp->signature, "RSD PTR ", 8) || !checksum(rsdp, 20))
        return false;
    bool extended = rsdp->revision >= 2;
    if (extended) {
        rsdp = x64_rsdp(sizeof(*rsdp));
        if (!rsdp || rsdp->length < sizeof(*rsdp) || rsdp->length > 4096)
            return false;
        rsdp = x64_rsdp(rsdp->length);
        if (!rsdp || !checksum(rsdp, rsdp->length))
            return false;
    }
    unsigned width = extended && rsdp->xsdt ? 8 : 4;
    const struct acpi_header *root = table(width == 8 ? rsdp->xsdt : rsdp->rsdt);
    if (!root || memcmp(root->signature, width == 8 ? "XSDT" : "RSDT", 4) ||
        (root->length - sizeof(*root)) % width)
        return false;
    const uint8_t *entries = (const uint8_t *)(root + 1);
    size_t count = (root->length - sizeof(*root)) / width;
    for (size_t i = 0; i < count; i++) {
        const struct acpi_header *entry =
            table(width == 8 ? u64(entries + i * width) : u32(entries + i * width));
        if (entry && !memcmp(entry->signature, "APIC", 4)) {
            if (entry->length < 44)
                return false;
            madt = entry;
            kprintf("[acpi] %s and MADT checksums valid\n", width == 8 ? "XSDT" : "RSDT");
            return true;
        }
    }
    return false;
}

static bool flags_valid(uint16_t flags) {
    return !(flags & ~15u) && (flags & 3) != 2 && ((flags >> 2) & 3) != 2;
}

bool x64_acpi_lapic_address(uint64_t *address) {
    if (!find_madt())
        return false;
    *address = u32((const uint8_t *)madt + 36);
    const uint8_t *p = (const uint8_t *)madt + 44;
    const uint8_t *end = (const uint8_t *)madt + madt->length;
    while (p < end) {
        if (end - p < 2 || p[1] < 2 || p[1] > end - p)
            return false;
        if (p[0] == 5) {
            if (p[1] < 12)
                return false;
            *address = u64(p + 4);
        }
        p += p[1];
    }
    return *address && !(*address & 4095);
}

bool x64_acpi_init(struct x64_acpi_info *info, uint32_t bsp_id) {
    memset(info, 0, sizeof(*info));
    if (!x64_acpi_lapic_address(&info->lapic_address))
        return false;
    for (unsigned i = 0; i < 16; i++)
        info->isa[i].gsi = i;
    const uint8_t *p = (const uint8_t *)madt + 44;
    const uint8_t *end = (const uint8_t *)madt + madt->length;
    while (p < end) {
        if (end - p < 2 || p[1] < 2 || p[1] > end - p)
            return false;
        switch (p[0]) {
        case 0:
            if (p[1] < 8)
                return false;
            if (u32(p + 4) & 1) {
                info->cpu_count++;
                if (p[3] == bsp_id) {
                    info->bsp_uid = p[2];
                    info->bsp_found = true;
                }
            }
            break;
        case 1:
            if (p[1] < 12 || info->ioapic_count == 8 || !u32(p + 4) || (u32(p + 4) & 4095))
                return false;
            info->ioapics[info->ioapic_count++] = (struct x64_ioapic_info){u32(p + 4), u32(p + 8)};
            break;
        case 2:
            if (p[1] < 10 || p[2] != 0 || p[3] >= 16 || !flags_valid(u16(p + 8)))
                return false;
            if (info->isa[p[3]].present)
                return false;
            info->isa[p[3]] = (struct x64_iso){u32(p + 4), u16(p + 8), true};
            break;
        case 3:
            if (p[1] < 8 || info->source_count == 16 || !flags_valid(u16(p + 2)))
                return false;
            info->sources[info->source_count++] = (struct x64_nmi_source){u32(p + 4), u16(p + 2)};
            break;
        case 4:
            if (p[1] < 6 || info->nmi_count == 32 || p[5] > 1 || !flags_valid(u16(p + 3)))
                return false;
            info->nmis[info->nmi_count++] =
                (struct x64_nmi){p[2] == 255 ? UINT32_MAX : p[2], p[5], u16(p + 3)};
            break;
        case 5:
            if (p[1] < 12)
                return false;
            break;
        case 9:
            if (p[1] < 16)
                return false;
            if (u32(p + 8) & 1) {
                info->cpu_count++;
                if (u32(p + 4) == bsp_id) {
                    info->bsp_uid = u32(p + 12);
                    info->bsp_found = true;
                }
            }
            break;
        case 10:
            if (p[1] < 12 || info->nmi_count == 32 || p[8] > 1 || !flags_valid(u16(p + 2)))
                return false;
            info->nmis[info->nmi_count++] = (struct x64_nmi){u32(p + 4), p[8], u16(p + 2)};
            break;
        default:
            break;
        }
        p += p[1];
    }
    return info->ioapic_count && info->bsp_found;
}
