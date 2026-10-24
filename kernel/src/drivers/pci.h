#ifndef PCI_H
#define PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pci_address {
    uint8_t bus, slot, function;
};

struct pci_bar {
    uint64_t address;
    bool io, wide, prefetchable;
};

struct pci_device {
    struct pci_address address;
    uint16_t vendor, device;
    uint8_t class_code, subclass, interface, revision, header_type;
    uint8_t irq_line, irq_pin;
    struct pci_bar bars[6];
};

typedef void (*pci_visitor)(const struct pci_device *device, void *context);

uint32_t pci_read32(struct pci_address address, uint8_t offset);
uint16_t pci_read16(struct pci_address address, uint8_t offset);
uint8_t pci_read8(struct pci_address address, uint8_t offset);
size_t pci_enumerate(pci_visitor visitor, void *context);

#endif
