#include "internal.h"
#include "console/kprintf.h"

#define IRQ_FIRST 64
#define IRQ_SLOTS 160
#define SHARED_HANDLERS 4

struct ioapic {
    volatile uint32_t *registers;
    uint32_t base, count;
};

struct irq_callback {
    hal_irq_handler function;
    void *context;
};

struct irq_route {
    uint32_t gsi;
    bool allocated, low, level;
    uint64_t count;
    struct irq_callback handlers[SHARED_HANDLERS];
};

static struct x64_acpi_info acpi;
static struct ioapic ioapics[8];
static struct irq_route routes[IRQ_SLOTS];
static volatile uint32_t *lapic;
static uint32_t bsp_id;
static bool x2apic, ready;
static uint64_t unhandled;

uint32_t x64_lapic_read(uint32_t offset) {
    return x2apic ? (uint32_t)rdmsr(0x800 + (offset >> 4)) : lapic[offset / 4];
}

void x64_lapic_write(uint32_t offset, uint32_t value) {
    if (x2apic)
        wrmsr(0x800 + (offset >> 4), value);
    else {
        lapic[offset / 4] = value;
        (void)lapic[0x20 / 4];
    }
}

bool x64_apic_ready(void) {
    return ready;
}

static uint32_t io_read(struct ioapic *io, uint8_t index) {
    io->registers[0] = index;
    __asm__ volatile("" : : : "memory");
    return io->registers[4];
}

static void io_write(struct ioapic *io, uint8_t index, uint32_t value) {
    io->registers[0] = index;
    __asm__ volatile("" : : : "memory");
    io->registers[4] = value;
    (void)io->registers[4];
}

static struct ioapic *controller(uint32_t gsi) {
    for (size_t i = 0; i < acpi.ioapic_count; i++)
        if (gsi >= ioapics[i].base && gsi - ioapics[i].base < ioapics[i].count)
            return &ioapics[i];
    return NULL;
}

static void route_write(struct ioapic *io, uint32_t gsi, uint32_t flags, bool masked) {
    uint8_t index = 0x10 + 2 * (gsi - io->base);
    io_write(io, index, io_read(io, index) | (1u << 16));
    io_write(io, index + 1, bsp_id << 24);
    io_write(io, index, flags | (masked ? 1u << 16 : 0));
}

static uint32_t polarity(uint16_t flags) {
    return ((flags & 3) == 3 ? 1u << 13 : 0) | (((flags >> 2) & 3) == 3 ? 1u << 15 : 0);
}

static void pic_disable(void) {
    out8(0x20, 0x11);
    out8(0x80, 0);
    out8(0xa0, 0x11);
    out8(0x80, 0);
    out8(0x21, 0x20);
    out8(0x80, 0);
    out8(0xa1, 0x28);
    out8(0x80, 0);
    out8(0x21, 4);
    out8(0x80, 0);
    out8(0xa1, 2);
    out8(0x80, 0);
    out8(0x21, 1);
    out8(0x80, 0);
    out8(0xa1, 1);
    out8(0x80, 0);
    out8(0x21, 0xff);
    out8(0xa1, 0xff);
}

bool hal_interrupt_controller_init(void) {
    if (ready)
        return false;
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    if (!(d & (1u << 9)))
        return false;
    uint64_t address;
    if (!x64_acpi_lapic_address(&address))
        return false;
    uint64_t apic_base = rdmsr(0x1b);
    x2apic = (apic_base & (1u << 10)) != 0;
    if (!x2apic) {
        wrmsr(0x1b, (apic_base & (1u << 8)) | address | (1u << 11));
        lapic = x64_map_mmio(address);
    }
    bsp_id = x64_lapic_read(0x20);
    if (!x2apic)
        bsp_id >>= 24;
    if (bsp_id > 255 || !x64_acpi_init(&acpi, bsp_id))
        return false;
    pic_disable();
    x64_lapic_write(0x80, 0);
    uint32_t version = x64_lapic_read(0x30);
    uint32_t max_lvt = (version >> 16) & 255;
    if (max_lvt < 3)
        return false;
    x64_lapic_write(0x320, 1u << 16);
    x64_lapic_write(0x350, 1u << 16);
    x64_lapic_write(0x360, 1u << 16);
    if (max_lvt >= 4)
        x64_lapic_write(0x340, 1u << 16);
    if (max_lvt >= 5)
        x64_lapic_write(0x330, 1u << 16);
    if (max_lvt >= 6)
        x64_lapic_write(0x2f0, 1u << 16);
    x64_lapic_write(0xf0, 0x100 | 255);
    x64_lapic_write(0x280, 0);
    (void)x64_lapic_read(0x280);
    x64_lapic_write(0x370, 254);
    for (size_t i = 0; i < acpi.nmi_count; i++) {
        struct x64_nmi *nmi = &acpi.nmis[i];
        if (nmi->uid == UINT32_MAX || nmi->uid == acpi.bsp_uid)
            x64_lapic_write(0x350 + nmi->lint * 16, (4u << 8) | polarity(nmi->flags));
    }
    for (size_t i = 0; i < acpi.ioapic_count; i++) {
        struct ioapic *io = &ioapics[i];
        io->registers = x64_map_mmio(acpi.ioapics[i].address);
        io->base = acpi.ioapics[i].gsi_base;
        io->count = ((io_read(io, 1) >> 16) & 255) + 1;
        if (io->count > 120 || io->base > UINT32_MAX - io->count)
            return false;
        for (size_t j = 0; j < i; j++)
            if (io->base < ioapics[j].base + ioapics[j].count &&
                ioapics[j].base < io->base + io->count)
                return false;
        for (uint32_t pin = 0; pin < io->count; pin++)
            route_write(io, io->base + pin, 0, true);
        kprintf("[ioapic] id=%u address=0x%x GSI=%u..%u masked\n", io_read(io, 0) >> 24,
                acpi.ioapics[i].address, io->base, io->base + io->count - 1);
    }
    for (unsigned i = 0; i < 16; i++)
        if (acpi.isa[i].present)
            kprintf("[ioapic] ISA IRQ%u -> GSI%u flags=0x%x\n", i, acpi.isa[i].gsi,
                    (unsigned)acpi.isa[i].flags);
    for (size_t i = 0; i < acpi.source_count; i++) {
        struct x64_nmi_source *source = &acpi.sources[i];
        struct ioapic *io = controller(source->gsi);
        if (!io)
            return false;
        route_write(io, source->gsi, (4u << 8) | polarity(source->flags), false);
    }
    ready = true;
    kprintf("[apic] %s BSP id=%u version=0x%x CPUs=%u, PIC masked\n", x2apic ? "x2APIC" : "xAPIC",
            bsp_id, version, acpi.cpu_count);
    return true;
}

static struct irq_route *find_route(uint32_t gsi) {
    for (unsigned i = 0; i < IRQ_SLOTS; i++)
        if (routes[i].allocated && routes[i].gsi == gsi)
            return &routes[i];
    return NULL;
}

bool hal_irq_register_gsi(uint32_t gsi, bool active_low, bool level, hal_irq_handler handler,
                          void *context) {
    if (!ready || !handler || !controller(gsi))
        return false;
    uint64_t state = hal_irq_save();
    for (size_t i = 0; i < acpi.source_count; i++) {
        if (acpi.sources[i].gsi == gsi) {
            hal_irq_restore(state);
            return false;
        }
    }
    struct irq_route *route = find_route(gsi);
    if (!route) {
        for (unsigned i = 0; i < IRQ_SLOTS; i++) {
            if (!routes[i].allocated) {
                route = &routes[i];
                *route = (struct irq_route){
                    .allocated = true, .gsi = gsi, .low = active_low, .level = level};
                route_write(controller(gsi), gsi,
                            (IRQ_FIRST + i) | (active_low ? 1u << 13 : 0) | (level ? 1u << 15 : 0),
                            true);
                break;
            }
        }
    }
    if (!route) {
        hal_irq_restore(state);
        return false;
    }
    if (route->low != active_low || route->level != level) {
        bool any = false;
        for (unsigned i = 0; i < SHARED_HANDLERS; i++)
            any |= route->handlers[i].function != NULL;
        if (any) {
            hal_irq_restore(state);
            return false;
        }
        route->low = active_low;
        route->level = level;
        unsigned vector = IRQ_FIRST + (unsigned)(route - routes);
        route_write(controller(gsi), gsi,
                    vector | (active_low ? 1u << 13 : 0) | (level ? 1u << 15 : 0), true);
    }
    for (unsigned i = 0; i < SHARED_HANDLERS; i++) {
        if (route->handlers[i].function == handler && route->handlers[i].context == context) {
            hal_irq_restore(state);
            return false;
        }
    }
    for (unsigned i = 0; i < SHARED_HANDLERS; i++) {
        if (!route->handlers[i].function) {
            route->handlers[i] = (struct irq_callback){handler, context};
            hal_irq_restore(state);
            return true;
        }
    }
    hal_irq_restore(state);
    return false;
}

bool hal_irq_unregister_gsi(uint32_t gsi, hal_irq_handler handler, void *context) {
    uint64_t state = hal_irq_save();
    struct irq_route *route = find_route(gsi);
    bool removed = false, any = false;
    if (route && handler) {
        for (unsigned i = 0; i < SHARED_HANDLERS; i++) {
            if (route->handlers[i].function == handler && route->handlers[i].context == context) {
                route->handlers[i] = (struct irq_callback){0};
                removed = true;
            }
            any |= route->handlers[i].function != NULL;
        }
        if (removed && !any) {
            hal_irq_mask_gsi(gsi, true);
        }
    }
    hal_irq_restore(state);
    return removed;
}

bool hal_irq_mask_gsi(uint32_t gsi, bool masked) {
    uint64_t state = hal_irq_save();
    struct irq_route *route = find_route(gsi);
    struct ioapic *io = controller(gsi);
    if (!ready || !route || !io) {
        hal_irq_restore(state);
        return false;
    }
    bool any = false;
    for (unsigned i = 0; i < SHARED_HANDLERS; i++)
        any |= route->handlers[i].function != NULL;
    if (!masked && !any) {
        hal_irq_restore(state);
        return false;
    }
    uint8_t index = 0x10 + 2 * (gsi - io->base);
    uint32_t value = io_read(io, index);
    io_write(io, index, masked ? value | (1u << 16) : value & ~(1u << 16));
    hal_irq_restore(state);
    return true;
}

bool hal_irq_register_isa(uint8_t irq, hal_irq_handler handler, void *context) {
    if (irq >= 16)
        return false;
    struct x64_iso *iso = &acpi.isa[irq];
    return hal_irq_register_gsi(iso->gsi, (iso->flags & 3) == 3, ((iso->flags >> 2) & 3) == 3,
                                handler, context);
}

bool hal_irq_unregister_isa(uint8_t irq, hal_irq_handler handler, void *context) {
    return irq < 16 && hal_irq_unregister_gsi(acpi.isa[irq].gsi, handler, context);
}

bool hal_irq_mask_isa(uint8_t irq, bool masked) {
    return irq < 16 && hal_irq_mask_gsi(acpi.isa[irq].gsi, masked);
}

uint64_t hal_irq_count_isa(uint8_t irq) {
    if (irq >= 16)
        return 0;
    uint64_t state = hal_irq_save();
    struct irq_route *route = find_route(acpi.isa[irq].gsi);
    uint64_t count = route ? route->count : 0;
    hal_irq_restore(state);
    return count;
}

uint64_t hal_irq_unhandled_count(void) {
    uint64_t state = hal_irq_save();
    uint64_t count = unhandled;
    hal_irq_restore(state);
    return count;
}

void x64_apic_dispatch(uint8_t vector) {
    if (vector == 255)
        return;
    if (!ready)
        hal_panic("interrupt before APIC initialization");
    if (vector == 48) {
        x64_timer_interrupt();
    } else if (vector == 254) {
        x64_lapic_write(0x280, 0);
        kprintf("[apic] ESR=0x%x\n", x64_lapic_read(0x280));
        hal_panic("local APIC error");
    } else if (vector >= IRQ_FIRST && vector < IRQ_FIRST + IRQ_SLOTS) {
        struct irq_route *route = &routes[vector - IRQ_FIRST];
        bool claimed = false;
        if (route->allocated) {
            route->count++;
            for (unsigned i = 0; i < SHARED_HANDLERS; i++) {
                struct irq_callback callback = route->handlers[i];
                if (callback.function)
                    claimed |= callback.function(callback.context);
            }
        }
        if (!claimed) {
            unhandled++;
            if (route->allocated)
                hal_irq_mask_gsi(route->gsi, true);
        }
    } else {
        unhandled++;
    }
    x64_lapic_write(0xb0, 0);
}
