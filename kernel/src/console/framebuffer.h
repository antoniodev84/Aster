#ifndef FRAMEBUFFER_CONSOLE_H
#define FRAMEBUFFER_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct framebuffer_info {
    void *address;
    size_t width, height, pitch;
    uint8_t red_size, red_shift;
    uint8_t green_size, green_shift;
    uint8_t blue_size, blue_shift;
};

bool framebuffer_console_init(const struct framebuffer_info *info);
void framebuffer_console_dimensions(size_t *columns, size_t *rows);
void framebuffer_console_putc(char c);

#endif
