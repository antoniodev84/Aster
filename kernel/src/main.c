#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include "hal/hal.h"
#include "drivers/ps2.h"
#include "drivers/pci.h"
#include "console/kprintf.h"
#include "console/framebuffer.h"
#include "mm/mm.h"
#include "proc/process.h"
#include "drivers/block.h"
#include "drivers/tty.h"
#include "fs/vfs.h"

__attribute__((used, section(".limine_requests"))) static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests"))) static volatile struct limine_framebuffer_request
    framebuffer_request = {.id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0};

__attribute__((
    used, section(".limine_requests"))) static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_memmap_request
    memmap_request = {.id = LIMINE_MEMMAP_REQUEST_ID};

__attribute__((
    used, section(".limine_requests"))) static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID};

__attribute__((
    used, section(".limine_requests"))) static volatile struct limine_executable_address_request
    executable_request = {.id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_module_request
    module_request = {.id = LIMINE_MODULE_REQUEST_ID};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_paging_mode_request
    paging_request = {.id = LIMINE_PAGING_MODE_REQUEST_ID, .mode = 0, .min_mode = 0, .max_mode = 0};

__attribute__((
    used,
    section(".limine_requests_start"))) static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((
    used, section(".limine_requests_end"))) static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

static struct hal_memory_region boot_regions[1024];

static bool initialize_framebuffer(void) {
    if (!framebuffer_request.response)
        return false;
    for (uint64_t i = 0; i < framebuffer_request.response->framebuffer_count; i++) {
        struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[i];
        if (fb->memory_model != LIMINE_FRAMEBUFFER_RGB || fb->bpp != 32)
            continue;
        struct framebuffer_info info = {.address = fb->address,
                                        .width = fb->width,
                                        .height = fb->height,
                                        .pitch = fb->pitch,
                                        .red_size = fb->red_mask_size,
                                        .red_shift = fb->red_mask_shift,
                                        .green_size = fb->green_mask_size,
                                        .green_shift = fb->green_mask_shift,
                                        .blue_size = fb->blue_mask_size,
                                        .blue_shift = fb->blue_mask_shift};
        if (framebuffer_console_init(&info))
            return true;
    }
    return false;
}

static void print_pci(const struct pci_device *device, void *context) {
    (void)context;
    kprintf(
        "[pci] %02x:%02x.%u vendor=%04x device=%04x class=%02x:%02x:%02x header=%u irq=%u pin=%u\n",
        (unsigned)device->address.bus, (unsigned)device->address.slot,
        (unsigned)device->address.function, (unsigned)device->vendor, (unsigned)device->device,
        (unsigned)device->class_code, (unsigned)device->subclass, (unsigned)device->interface,
        (unsigned)device->header_type, (unsigned)device->irq_line, (unsigned)device->irq_pin);
    for (unsigned i = 0; i < 6; i++) {
        const struct pci_bar *bar = &device->bars[i];
        if (bar->address)
            kprintf("[pci] BAR%u %s%u address=0x%llx prefetch=%u\n", i, bar->io ? "IO" : "MMIO",
                    bar->wide ? 64u : 32u, (unsigned long long)bar->address,
                    (unsigned)bar->prefetchable);
    }
}

void kmain(void) {
    hal_interrupts_disable();
    hal_debug_init();
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision))
        hal_panic("Limine revision 6 required");
    bool framebuffer_ready = initialize_framebuffer();
    kprintf("[kernel] boot\n[console] framebuffer=%s serial=COM1 115200\n",
            framebuffer_ready ? "ready" : "unavailable");
    if (!hhdm_request.response || !memmap_request.response || !rsdp_request.response ||
        !executable_request.response)
        hal_panic("missing Limine memory/ACPI information");
    struct limine_memmap_response *map = memmap_request.response;
    if (map->entry_count > 1024)
        hal_panic("boot memory map exceeds capacity");
    for (size_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *entry = map->entries[i];
        bool mapped = entry->type == LIMINE_MEMMAP_USABLE ||
                      entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
                      entry->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES ||
                      entry->type == LIMINE_MEMMAP_FRAMEBUFFER ||
                      entry->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE ||
                      entry->type == LIMINE_MEMMAP_ACPI_NVS ||
                      entry->type == LIMINE_MEMMAP_RESERVED_MAPPED;
        boot_regions[i] = (struct hal_memory_region){entry->base, entry->length, mapped,
                                                     entry->type == LIMINE_MEMMAP_USABLE};
    }
    struct hal_boot_info boot = {.hhdm = hhdm_request.response->offset,
                                 .kernel_physical = executable_request.response->physical_base,
                                 .kernel_virtual = executable_request.response->virtual_base,
                                 .rsdp = rsdp_request.response->address,
                                 .regions = boot_regions,
                                 .region_count = map->entry_count};
    hal_cpu_init(&boot);
    pmm_init(&boot);
    vmm_init();
    if (!hal_interrupt_controller_init())
        hal_panic("ACPI/APIC/IOAPIC initialization failed");
    if (!hal_timer_init(100))
        hal_panic("APIC timer calibration failed");
    hal_realtime_init();
    ps2_init();
    size_t devices = pci_enumerate(print_pci, NULL);
    kprintf("[pci] devices=%llu segment=0 config=CF8/CFC\n", (unsigned long long)devices);
    ahci_init();
    if (!module_request.response || !module_request.response->module_count)
        hal_panic("initrd module missing");
    struct limine_file *initrd = module_request.response->modules[0];
    vfs_init(initrd->address, initrd->size);
    tty_init();
    process_init();
    process_start("/sbin/init");
}
