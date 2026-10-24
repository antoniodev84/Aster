#include "internal.h"
#include "console/kprintf.h"

static uint64_t epoch_ms, start_ms;
static uint8_t cmos(uint8_t reg) {
    out8(0x70, reg);
    return in8(0x71);
}
static unsigned decimal_rtc(uint8_t n, bool binary) {
    return binary ? n : (n & 15) + 10 * (n >> 4);
}
static bool leap(unsigned year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
void hal_realtime_init(void) {
    uint8_t first[8], second[8];
    const uint8_t registers[] = {0, 2, 4, 7, 8, 9, 0x32, 0x0b};
    bool stable = false;
    for (unsigned attempt = 0; attempt < 8; attempt++) {
        unsigned wait = 1000000;
        while ((cmos(0x0a) & 0x80) && --wait) {
        }
        if (!wait)
            break;
        for (unsigned i = 0; i < 8; i++)
            first[i] = cmos(registers[i]);
        wait = 1000000;
        while ((cmos(0x0a) & 0x80) && --wait) {
        }
        if (!wait)
            break;
        for (unsigned i = 0; i < 8; i++)
            second[i] = cmos(registers[i]);
        stable = true;
        for (unsigned i = 0; i < 8; i++)
            if (first[i] != second[i])
                stable = false;
        if (stable)
            break;
    }
    if (!stable) {
        kprintf("[clock] RTC unavailable\n");
        return;
    }
    bool binary = first[7] & 4;
    unsigned sec = decimal_rtc(first[0], binary), min = decimal_rtc(first[1], binary),
             hour = decimal_rtc(first[2] & 0x7f, binary), day = decimal_rtc(first[3], binary),
             month = decimal_rtc(first[4], binary), year = decimal_rtc(first[5], binary),
             century = decimal_rtc(first[6], binary);
    if (!(first[7] & 2)) {
        hour %= 12;
        if (first[2] & 0x80)
            hour += 12;
    }
    if (century < 19 || century > 99)
        century = 20;
    year += century * 100;
    if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
        min > 59 || sec > 59) {
        kprintf("[clock] invalid RTC date\n");
        return;
    }
    static const unsigned lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint64_t days = 0;
    for (unsigned y = 1970; y < year; y++)
        days += 365 + leap(y);
    for (unsigned m = 1; m < month; m++)
        days += lengths[m - 1] + (m == 2 && leap(year));
    days += day - 1;
    epoch_ms = ((days * 24 + hour) * 60 + min) * 60000 + (uint64_t)sec * 1000;
    start_ms = hal_time_ms();
    kprintf("[clock] RTC UTC %04u-%02u-%02u %02u:%02u:%02u\n", year, month, day, hour, min, sec);
}
uint64_t hal_realtime_ms(void) {
    return epoch_ms ? epoch_ms + hal_time_ms() - start_ms : 0;
}
