#include "net.h"
#include "pci.h"
#include "mm/mm.h"
#include "memory.h"
#include "console/kprintf.h"

#define RING_SIZE 32
struct e1000_rx {
    uint64_t address;
    uint16_t length, checksum;
    volatile uint8_t status;
    uint8_t errors;
    uint16_t special;
};
struct e1000_tx {
    uint64_t address;
    uint16_t length;
    uint8_t cso, command;
    volatile uint8_t status;
    uint8_t css;
    uint16_t special;
};
struct nic {
    bool intel;
    volatile uint32_t *mmio;
    uint16_t io;
    uint64_t ring, buffers, rtl_rx, rtl_tx;
    unsigned rx, tx;
    struct net_interface *interface;
};
static struct nic cards[4];
struct net_interface ethernet_interfaces[4];
unsigned ethernet_count;
static uint16_t in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static uint32_t in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static void out16(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0,%1" ::"a"(value), "Nd"(port));
}
static void out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0,%1" ::"a"(value), "Nd"(port));
}
static void fence(void) {
    __asm__ volatile("mfence" ::: "memory");
}
static void reg_write(struct nic *n, unsigned reg, uint32_t value) {
    n->mmio[reg / 4] = value;
}
static uint32_t reg_read(struct nic *n, unsigned reg) {
    return n->mmio[reg / 4];
}
long ethernet_send(unsigned index, const void *frame, size_t length) {
    if (index >= ethernet_count || length > 1514 || length < 14)
        return -EINVAL;
    struct nic *n = &cards[index];
    if (!n->interface->link || !n->interface->up)
        return -ENETDOWN;
    if (n->intel) {
        struct e1000_tx *ring = physical_pointer(n->ring + 2048);
        unsigned slot = n->tx;
        if (!(ring[slot].status & 1))
            return -EAGAIN;
        void *buffer = physical_pointer(n->buffers + (RING_SIZE + slot) * 2048);
        memcpy(buffer, frame, length);
        if (length < 60)
            memset((uint8_t *)buffer + length, 0, 60 - length);
        ring[slot].length = length < 60 ? 60 : length;
        ring[slot].command = 0x0b;
        ring[slot].status = 0;
        fence();
        n->tx = (slot + 1) % RING_SIZE;
        reg_write(n, 0x3818, n->tx);
    } else {
        unsigned slot = n->tx;
        if (!(in32(n->io + 0x10 + slot * 4) & 0x2000))
            return -EAGAIN;
        void *buffer = physical_pointer(n->rtl_tx + slot * 2048);
        memcpy(buffer, frame, length);
        if (length < 60)
            memset((uint8_t *)buffer + length, 0, 60 - length);
        fence();
        out32(n->io + 0x10 + slot * 4, length < 60 ? 60 : length);
        n->tx = (slot + 1) % 4;
    }
    return 0;
}
static void read_mac(struct nic *n, struct net_interface *interface) {
    if (n->intel) {
        uint32_t lo = reg_read(n, 0x5400), hi = reg_read(n, 0x5404);
        for (unsigned i = 0; i < 4; i++)
            interface->mac[i] = lo >> (8 * i);
        interface->mac[4] = hi;
        interface->mac[5] = hi >> 8;
    } else {
        for (unsigned i = 0; i < 6; i++)
            interface->mac[i] = hal_io_read8(n->io + i);
    }
}
static void free_dma(struct nic *n) {
    if (n->ring)
        pmm_free(n->ring, 1);
    if (n->buffers)
        pmm_free(n->buffers, 32);
    if (n->rtl_rx)
        pmm_free(n->rtl_rx, 17);
    if (n->rtl_tx)
        pmm_free(n->rtl_tx, 2);
    n->ring = n->buffers = n->rtl_rx = n->rtl_tx = 0;
}
static void discover(const struct pci_device *d, void *context) {
    (void)context;
    bool intel = d->vendor == 0x8086 && (d->device == 0x100e || d->device == 0x100f);
    bool rtl = d->vendor == 0x10ec && d->device == 0x8139;
    if ((!intel && !rtl) || ethernet_count == 4)
        return;
    struct nic *n = &cards[ethernet_count];
    *n = (struct nic){.intel = intel};
    uint32_t command = pci_read16(d->address, 4) | 0x404;
    if (intel) {
        if (!d->bars[0].address || d->bars[0].io)
            return;
        n->mmio = mmio_map(d->bars[0].address, 0x6000);
        if (!n->mmio)
            return;
        command |= 2;
    } else {
        for (unsigned i = 0; i < 6; i++)
            if (d->bars[i].io && d->bars[i].address && d->bars[i].address <= 0xff00)
                n->io = d->bars[i].address;
        if (!n->io)
            return;
        command |= 1;
    }
    hal_pci_config_write32(d->address.bus, d->address.slot, d->address.function, 4, command);
    if (intel) {
        reg_write(n, 0xd8, UINT32_MAX);
        reg_write(n, 0, reg_read(n, 0) | (1u << 26));
        unsigned limit = 1000000;
        while ((reg_read(n, 0) & (1u << 26)) && --limit)
            __asm__ volatile("pause");
        if (!limit)
            return;
        reg_write(n, 0xd8, UINT32_MAX);
        reg_read(n, 0xc0);
        reg_write(n, 0, reg_read(n, 0) | 0x40);
        n->ring = pmm_alloc(1);
        n->buffers = pmm_alloc(32);
        if (!n->ring || !n->buffers) {
            free_dma(n);
            return;
        }
        memset(physical_pointer(n->ring), 0, 4096);
        struct e1000_rx *rx = physical_pointer(n->ring);
        struct e1000_tx *tx = physical_pointer(n->ring + 2048);
        for (unsigned i = 0; i < RING_SIZE; i++) {
            rx[i].address = n->buffers + i * 2048;
            tx[i].address = n->buffers + (RING_SIZE + i) * 2048;
            tx[i].status = 1;
        }
        for (unsigned i = 0; i < 128; i++)
            reg_write(n, 0x5200 + i * 4, 0);
        reg_write(n, 0x2800, n->ring);
        reg_write(n, 0x2804, n->ring >> 32);
        reg_write(n, 0x2808, RING_SIZE * sizeof(*rx));
        reg_write(n, 0x2810, 0);
        reg_write(n, 0x2818, RING_SIZE - 1);
        reg_write(n, 0x3800, n->ring + 2048);
        reg_write(n, 0x3804, (n->ring + 2048) >> 32);
        reg_write(n, 0x3808, RING_SIZE * sizeof(*tx));
        reg_write(n, 0x3810, 0);
        reg_write(n, 0x3818, 0);
        reg_write(n, 0x410, 0x0060200a);
        fence();
        reg_write(n, 0x100, (1u << 1) | (1u << 15) | (1u << 26));
        reg_write(n, 0x400, (1u << 1) | (1u << 3) | (15u << 4) | (64u << 12));
    } else {
        hal_io_write8(n->io + 0x52, 0);
        hal_io_write8(n->io + 0x37, 0x10);
        unsigned limit = 1000000;
        while ((hal_io_read8(n->io + 0x37) & 0x10) && --limit)
            __asm__ volatile("pause");
        if (!limit)
            return;
        n->rtl_rx = pmm_alloc_below(17, 1ull << 32);
        n->rtl_tx = pmm_alloc_below(2, 1ull << 32);
        if (!n->rtl_rx || !n->rtl_tx) {
            free_dma(n);
            return;
        }
        out32(n->io + 0x30, n->rtl_rx);
        for (unsigned i = 0; i < 4; i++)
            out32(n->io + 0x20 + i * 4, n->rtl_tx + i * 2048);
        out16(n->io + 0x3c, 0);
        out16(n->io + 0x3e, 0xffff);
        out32(n->io + 0x44, 0xf | (7u << 8) | (3u << 11) | (7u << 13));
        out32(n->io + 0x40, (3u << 24) | (7u << 8));
        hal_io_write8(n->io + 0x37, 0x0c);
    }
    n->interface = &ethernet_interfaces[ethernet_count];
    n->interface->up = true;
    read_mac(n, n->interface);
    ethernet_count++;
    kprintf("[net] en%u driver=%s MAC=%02x:%02x:%02x:%02x:%02x:%02x\n", ethernet_count,
            intel ? "e1000" : "rtl8139", n->interface->mac[0], n->interface->mac[1],
            n->interface->mac[2], n->interface->mac[3], n->interface->mac[4], n->interface->mac[5]);
}
void ethernet_init(void) {
    pci_enumerate(discover, NULL);
}
static void receive(struct nic *n, const void *data, size_t length) {
    if (length >= 14 && length <= 1514)
        net_ethernet_input((unsigned)(n - cards), data, length);
}
void ethernet_poll(void) {
    for (unsigned i = 0; i < ethernet_count; i++) {
        struct nic *n = &cards[i];
        bool link = n->intel ? (reg_read(n, 8) & 2) : !(hal_io_read8(n->io + 0x58) & 4);
        n->interface->link = link;
        if (n->intel) {
            struct e1000_rx *ring = physical_pointer(n->ring);
            for (unsigned budget = 0; budget < RING_SIZE; budget++) {
                unsigned slot = n->rx;
                if (!(ring[slot].status & 1))
                    break;
                fence();
                if ((ring[slot].status & 2) && !ring[slot].errors && ring[slot].length <= 2048)
                    receive(n, physical_pointer(ring[slot].address), ring[slot].length);
                ring[slot].status = 0;
                fence();
                reg_write(n, 0x2818, slot);
                n->rx = (slot + 1) % RING_SIZE;
            }
            reg_read(n, 0xc0);
        } else {
            uint8_t *ring = physical_pointer(n->rtl_rx);
            for (unsigned budget = 0; budget < 32 && !(hal_io_read8(n->io + 0x37) & 1); budget++) {
                fence();
                uint16_t status, length;
                memcpy(&status, ring + n->rx, 2);
                memcpy(&length, ring + n->rx + 2, 2);
                if (length == 0xfff0)
                    break;
                if (!(status & 1) || length < 18 || length > 1518) {
                    hal_io_write8(n->io + 0x37, 4);
                    out32(n->io + 0x30, n->rtl_rx);
                    n->rx = 0;
                    out16(n->io + 0x38, 0xfff0);
                    hal_io_write8(n->io + 0x37, 0x0c);
                    break;
                }
                uint8_t frame[1514];
                for (unsigned j = 0; j < (unsigned)length - 4; j++)
                    frame[j] = ring[(n->rx + 4 + j) & 65535];
                receive(n, frame, length - 4);
                n->rx = (n->rx + length + 4 + 3) & ~3u;
                n->rx &= 65535;
                out16(n->io + 0x38, n->rx - 16);
            }
            out16(n->io + 0x3e, in16(n->io + 0x3e));
        }
    }
}
