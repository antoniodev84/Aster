#include "pci.h"
#include "hal/hal.h"

uint32_t pci_read32(struct pci_address address, uint8_t offset) {
    return hal_pci_config_read32(address.bus, address.slot, address.function, offset);
}

uint16_t pci_read16(struct pci_address address, uint8_t offset) {
    if (offset & 1)
        return UINT16_MAX;
    return pci_read32(address, offset & ~3u) >> ((offset & 2) * 8);
}

uint8_t pci_read8(struct pci_address address, uint8_t offset) {
    return pci_read32(address, offset & ~3u) >> ((offset & 3) * 8);
}

static void read_device(struct pci_device *device, struct pci_address address) {
    *device = (struct pci_device){.address = address};
    uint32_t id = pci_read32(address, 0);
    uint32_t type = pci_read32(address, 8);
    device->vendor = id;
    device->device = id >> 16;
    device->class_code = type >> 24;
    device->subclass = type >> 16;
    device->interface = type >> 8;
    device->revision = type;
    device->header_type = pci_read8(address, 0x0e) & 0x7f;
    device->irq_line = pci_read8(address, 0x3c);
    device->irq_pin = pci_read8(address, 0x3d);
    unsigned bar_count = device->header_type == 0 ? 6 : device->header_type == 1 ? 2 : 0;
    for (unsigned i = 0; i < bar_count; i++) {
        uint32_t raw = pci_read32(address, 0x10 + i * 4);
        struct pci_bar *bar = &device->bars[i];
        bar->io = raw & 1;
        if (bar->io) {
            bar->address = raw & ~3u;
        } else {
            unsigned memory_type = (raw >> 1) & 3;
            bar->prefetchable = raw & 8;
            bar->wide = memory_type == 2;
            if (memory_type == 3)
                continue;
            bar->address = raw & ~15u;
            if (bar->wide) {
                if (i + 1 == bar_count) {
                    *bar = (struct pci_bar){0};
                    continue;
                }
                bar->address |= (uint64_t)pci_read32(address, 0x10 + (i + 1) * 4) << 32;
                i++;
            }
        }
    }
}

size_t pci_enumerate(pci_visitor visitor, void *context) {
    size_t count = 0;
    for (unsigned bus = 0; bus < 256; bus++) {
        for (unsigned slot = 0; slot < 32; slot++) {
            struct pci_address address = {bus, slot, 0};
            if (pci_read16(address, 0) == UINT16_MAX)
                continue;
            unsigned functions = pci_read8(address, 0x0e) & 0x80 ? 8 : 1;
            for (unsigned function = 0; function < functions; function++) {
                address.function = function;
                if (pci_read16(address, 0) == UINT16_MAX)
                    continue;
                struct pci_device device;
                read_device(&device, address);
                count++;
                if (visitor)
                    visitor(&device, context);
            }
        }
    }
    return count;
}
