#include "block.h"
#include "pci.h"
#include "mm/mm.h"
#include "memory.h"
#include "console/kprintf.h"
#include "proc/process.h"

struct block_device block_devices[32];
size_t block_device_count;

struct ahci_port {
    volatile uint32_t *registers;
    uint64_t commands, dma;
    bool wide;
};

struct command_header {
    uint16_t flags, prdt_length;
    volatile uint32_t transferred;
    uint64_t table;
    uint32_t reserved[4];
};
struct prdt {
    uint64_t address;
    uint32_t reserved, count;
};
struct command_table {
    uint8_t fis[64], atapi[16], reserved[48];
    struct prdt prdt;
};
_Static_assert(sizeof(struct command_header) == 32, "AHCI command header");
_Static_assert(sizeof(struct command_table) == 144, "AHCI command table");

static bool wait_clear(volatile uint32_t *reg, uint32_t mask) {
    for (unsigned i = 0; i < 10000000; i++) {
        if (!(*reg & mask))
            return true;
        __asm__ volatile("pause");
    }
    return false;
}

static bool delay_reset(void) {
    uint8_t speaker = hal_io_read8(0x61);
    hal_io_write8(0x61, speaker & ~3u);
    hal_io_write8(0x43, 0xb0);
    hal_io_write8(0x42, 1194 & 255);
    hal_io_write8(0x42, 1194 >> 8);
    hal_io_write8(0x61, (speaker & ~2u) | 1);
    unsigned timeout = 10000000;
    while (!(hal_io_read8(0x61) & 0x20) && --timeout)
        __asm__ volatile("pause");
    hal_io_write8(0x61, speaker);
    return timeout != 0;
}

static bool issue(struct ahci_port *port, uint8_t command, uint64_t lba, uint16_t count,
                  size_t length, bool write) {
    volatile uint32_t *p = port->registers;
    if (length > 65536 || !wait_clear(p + 8, 0x88) || p[14] || p[13])
        return false;
    struct command_header *header = physical_pointer(port->commands);
    struct command_table *table = physical_pointer(port->commands + 1280);
    memset(table, 0, sizeof(*table));
    header[0] = (struct command_header){.flags = 5 | (write ? 1u << 6 : 0),
                                        .prdt_length = length ? 1 : 0,
                                        .table = port->commands + 1280};
    table->fis[0] = 0x27;
    table->fis[1] = 0x80;
    table->fis[2] = command;
    table->fis[4] = lba;
    table->fis[5] = lba >> 8;
    table->fis[6] = lba >> 16;
    table->fis[7] = 0x40;
    table->fis[8] = lba >> 24;
    table->fis[9] = lba >> 32;
    table->fis[10] = lba >> 40;
    table->fis[12] = count;
    table->fis[13] = count >> 8;
    if (length)
        table->prdt = (struct prdt){.address = port->dma, .count = (uint32_t)length - 1};
    p[4] = UINT32_MAX;
    p[12] = UINT32_MAX;
    __asm__ volatile("mfence" : : : "memory");
    p[14] = 1;
    for (unsigned i = 0; i < 10000000; i++) {
        if (p[4] & (1u << 30))
            break;
        if (!(p[14] & 1)) {
            __asm__ volatile("mfence" : : : "memory");
            bool complete = !(p[8] & 0x21) && (!length || header[0].transferred == length);
            if (!complete)
                kprintf("[ahci] short/error DMA command=0x%x bytes=%u expected=%zu TFD=0x%x\n",
                        (unsigned)command, header[0].transferred, length, p[8]);
            return complete;
        }
        __asm__ volatile("pause");
    }
    kprintf("[ahci] command=0x%x failed IS=0x%x TFD=0x%x CI=0x%x\n", (unsigned)command, p[4], p[8],
            p[14]);
    return false;
}

static bool transfer(struct block_device *device, uint64_t lba, uint32_t sectors, void *buffer,
                     bool write) {
    if (!sectors || lba >= device->sectors || sectors > device->sectors - lba)
        return false;
    struct ahci_port *port = device->driver;
    uint8_t *bytes = buffer;
    uint64_t state = hal_irq_save();
    while (sectors) {
        uint16_t chunk = sectors > 128 ? 128 : sectors;
        size_t length = (size_t)chunk * 512;
        if (write)
            memcpy(physical_pointer(port->dma), bytes, length);
        if (!issue(port, write ? 0x35 : 0x25, lba, chunk, length, write)) {
            hal_irq_restore(state);
            return false;
        }
        struct process *task = process_current();
        if (task) {
            if (write)
                task->outblock++;
            else
                task->inblock++;
        }
        if (!write)
            memcpy(bytes, physical_pointer(port->dma), length);
        bytes += length;
        lba += chunk;
        sectors -= chunk;
    }
    hal_irq_restore(state);
    return true;
}

static bool flush(struct block_device *device) {
    uint64_t state = hal_irq_save();
    bool result = issue(device->driver, 0xea, 0, 0, 0, false);
    hal_irq_restore(state);
    return result;
}

static void discover(const struct pci_device *device, void *context) {
    (void)context;
    if (device->class_code != 1 || device->subclass != 6 || device->interface != 1 ||
        !device->bars[5].address || device->bars[5].io)
        return;
    uint32_t command = pci_read32(device->address, 4);
    hal_pci_config_write32(device->address.bus, device->address.slot, device->address.function, 4,
                           (command & 0xffff) | 6);
    volatile uint32_t *hba = mmio_map(device->bars[5].address, 0x1100);
    if (!hba) {
        kprintf("[ahci] ABAR mapping failed\n");
        return;
    }
    if (hba[9] & 1) {
        hba[10] |= 2;
        if (!wait_clear(hba + 10, 0x11)) {
            kprintf("[ahci] BIOS handoff timed out\n");
            return;
        }
    }
    hba[1] = 0x80000001;
    if (!wait_clear(hba + 1, 1)) {
        kprintf("[ahci] reset timed out\n");
        return;
    }
    hba[1] = 0x80000000;
    bool wide = (hba[0] & (1u << 31)) != 0;
    uint32_t ports = hba[3];
    for (unsigned i = 0; i < 32 && block_device_count < 32; i++) {
        if (!(ports & (1u << i)))
            continue;
        volatile uint32_t *p = hba + 0x40 + i * 0x20;
        kprintf("[ahci] port=%u SSTS=0x%x SIG=0x%x CMD=0x%x TFD=0x%x\n", i, p[10], p[9], p[6],
                p[8]);
        if ((p[10] & 0xf) != 3)
            continue;
        p[6] &= ~1u;
        if (!wait_clear(p + 6, 1u << 15))
            continue;
        p[6] &= ~(1u << 4);
        if (!wait_clear(p + 6, 1u << 14))
            continue;
        struct ahci_port *port = kcalloc(1, sizeof(*port));
        if (!port)
            break;
        port->registers = p;
        port->wide = wide;
        port->commands = wide ? pmm_alloc(1) : pmm_alloc_below(1, 1ull << 32);
        port->dma = wide ? pmm_alloc(16) : pmm_alloc_below(16, 1ull << 32);
        if (!port->commands || !port->dma ||
            (!wide && (port->commands >> 32 || port->dma + 65535 > UINT32_MAX))) {
            if (port->commands)
                pmm_free(port->commands, 1);
            if (port->dma)
                pmm_free(port->dma, 16);
            kfree(port);
            continue;
        }
        p[0] = port->commands;
        p[1] = port->commands >> 32;
        p[2] = port->commands + 1024;
        p[3] = (port->commands + 1024) >> 32;
        p[4] = p[12] = UINT32_MAX;
        p[5] = 0;
        p[6] |= 0x10;
        p[11] = (p[11] & ~0xfu) | 0x301;
        bool reset_ok = delay_reset();
        p[11] = (p[11] & ~0xfu) | 0x300;
        reset_ok = reset_ok && wait_clear(p + 8, 0x88);
        kprintf("[ahci] port=%u reset=%s SIG=0x%x TFD=0x%x\n", i, reset_ok ? "ready" : "failed",
                p[9], p[8]);
        if (!reset_ok || p[9] != 0x101) {
            p[6] &= ~0x10u;
            wait_clear(p + 6, 1u << 14);
            pmm_free(port->commands, 1);
            pmm_free(port->dma, 16);
            kfree(port);
            continue;
        }
        p[6] |= 1;
        if (!issue(port, 0xec, 0, 0, 512, false)) {
            p[6] &= ~1u;
            wait_clear(p + 6, 1u << 15);
            p[6] &= ~(1u << 4);
            wait_clear(p + 6, 1u << 14);
            pmm_free(port->commands, 1);
            pmm_free(port->dma, 16);
            kfree(port);
            continue;
        }
        uint16_t *id = physical_pointer(port->dma);
        if (!(id[83] & (1u << 10))) {
            kprintf("[ahci] LBA48 required on port %u\n", i);
            continue;
        }
        uint64_t sectors = (uint64_t)id[100] | (uint64_t)id[101] << 16 | (uint64_t)id[102] << 32 |
                           (uint64_t)id[103] << 48;
        struct block_device *disk = &block_devices[block_device_count];
        *disk = (struct block_device){.name = "sda",
                                      .sectors = sectors,
                                      .driver = port,
                                      .transfer = transfer,
                                      .flush = flush};
        disk->name[2] += block_device_count++;
        kprintf("[ahci] %s port=%u sectors=%llu DMA%u\n", disk->name, i,
                (unsigned long long)sectors, wide ? 64u : 32u);
    }
}

void ahci_init(void) {
    pci_enumerate(discover, NULL);
}
bool block_read(struct block_device *device, uint64_t lba, uint32_t sectors, void *buffer) {
    return device && device->transfer(device, lba, sectors, buffer, false);
}
bool block_write(struct block_device *device, uint64_t lba, uint32_t sectors, const void *buffer) {
    return device && device->transfer(device, lba, sectors, (void *)buffer, true);
}
