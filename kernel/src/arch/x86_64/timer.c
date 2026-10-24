#include "internal.h"
#include "console/kprintf.h"

static volatile uint64_t ticks;
static uint32_t frequency;
static uint64_t tsc_frequency, tsc_origin;
static uint64_t tsc_read(void) {
    uint32_t lo, hi;
    __asm__ volatile("lfence;rdtsc" : "=a"(lo), "=d"(hi)::"memory");
    return (uint64_t)hi << 32 | lo;
}
uint64_t hal_counter_ns(void) {
    if (!tsc_frequency)
        return ticks * 10000000ull;
    uint64_t elapsed = tsc_read() - tsc_origin;
    return elapsed / tsc_frequency * 1000000000ull +
           (elapsed % tsc_frequency) * 1000000000ull / tsc_frequency;
}

bool hal_timer_init(uint32_t hz) {
    if (!x64_apic_ready() || hz < 10 || hz > 10000 || frequency)
        return false;
    uint64_t state = hal_irq_save();
    uint8_t speaker = in8(0x61);
    out8(0x61, speaker & ~3u);
    out8(0x43, 0xb0);
    const uint16_t reference = 11932;
    out8(0x42, reference & 255);
    out8(0x42, reference >> 8);
    x64_lapic_write(0x320, (1u << 16) | 48);
    x64_lapic_write(0x3e0, 3);
    x64_lapic_write(0x380, UINT32_MAX);
    uint64_t first_tsc = tsc_read();
    out8(0x61, (speaker & ~2u) | 1);
    unsigned timeout = 50000000;
    while (!(in8(0x61) & 0x20) && --timeout)
        __asm__ volatile("pause");
    uint64_t final_tsc = tsc_read();
    uint32_t elapsed = UINT32_MAX - x64_lapic_read(0x390);
    x64_lapic_write(0x380, 0);
    out8(0x61, speaker);
    uint64_t initial = (uint64_t)elapsed * 1193182 / ((uint64_t)reference * hz);
    if (!timeout || !elapsed || !initial || initial > UINT32_MAX) {
        hal_irq_restore(state);
        return false;
    }
    uint64_t calibrated = (final_tsc - first_tsc) * 1193182 / reference;
    if (calibrated >= 1000000 && calibrated <= 10000000000ull) {
        tsc_frequency = calibrated;
        tsc_origin = final_tsc;
    }
    ticks = 0;
    frequency = hz;
    x64_lapic_write(0x320, (1u << 17) | 48);
    x64_lapic_write(0x380, (uint32_t)initial);
    kprintf("[timer] LAPIC periodic hz=%u initial=%u divisor=16 calibrated=PIT2\n", hz,
            (unsigned)initial);
    hal_irq_restore(state);
    return true;
}

void x64_timer_interrupt(void) {
    ticks++;
}
uint64_t hal_timer_ticks(void) {
    return ticks;
}
uint64_t hal_time_ms(void) {
    if (tsc_frequency)
        return hal_counter_ns() / 1000000;
    uint64_t value = ticks;
    if (!frequency)
        return 0;
    return value / frequency * 1000 + value % frequency * 1000 / frequency;
}
