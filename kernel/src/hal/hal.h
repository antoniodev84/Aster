#ifndef HAL_H
#define HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct hal_memory_region {
    uint64_t base;
    uint64_t length;
    bool mapped;
    bool usable;
};

struct hal_boot_info {
    uint64_t hhdm;
    uint64_t kernel_physical;
    uint64_t kernel_virtual;
    const void *rsdp;
    const struct hal_memory_region *regions;
    size_t region_count;
};

typedef bool (*hal_irq_handler)(void *context);

void hal_debug_init(void);
void hal_debug_putc(char c);
void hal_cpu_init(const struct hal_boot_info *boot);
bool hal_interrupt_controller_init(void);
uint64_t hal_irq_save(void);
void hal_irq_restore(uint64_t state);
void hal_interrupts_enable(void);
void hal_interrupts_disable(void);
void hal_idle(void);
_Noreturn void hal_panic(const char *message);
bool hal_irq_register_isa(uint8_t irq, hal_irq_handler handler, void *context);
bool hal_irq_unregister_isa(uint8_t irq, hal_irq_handler handler, void *context);
bool hal_irq_register_gsi(uint32_t gsi, bool active_low, bool level, hal_irq_handler handler,
                          void *context);
bool hal_irq_unregister_gsi(uint32_t gsi, hal_irq_handler handler, void *context);
bool hal_irq_mask_isa(uint8_t irq, bool masked);
bool hal_irq_mask_gsi(uint32_t gsi, bool masked);
uint64_t hal_irq_count_isa(uint8_t irq);
uint64_t hal_irq_unhandled_count(void);
bool hal_timer_init(uint32_t hz);
uint64_t hal_timer_ticks(void);
uint64_t hal_time_ms(void);
uint64_t hal_counter_ns(void);
void hal_realtime_init(void);
uint64_t hal_realtime_ms(void);
uint8_t hal_io_read8(uint16_t port);
void hal_io_write8(uint16_t port, uint8_t value);
uint32_t hal_pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void hal_pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset,
                            uint32_t value);

#endif
