#include "framebuffer.h"
#include "font.h"

#define GLYPH_WIDTH 8
#define GLYPH_HEIGHT 16
#define MARGIN 8

static struct framebuffer_info fb;
static size_t column, row, columns, rows;
static uint32_t foreground, background;
static bool ready;
static unsigned escape_state, parameters[8], parameter_count;
static size_t saved_row, saved_column;
static bool inverse;

static uint32_t channel(uint8_t value, uint8_t size, uint8_t shift) {
    return (uint32_t)((uint64_t)value * ((1u << size) - 1) / 255) << shift;
}

static uint32_t color(uint8_t red, uint8_t green, uint8_t blue) {
    return channel(red, fb.red_size, fb.red_shift) | channel(green, fb.green_size, fb.green_shift) |
           channel(blue, fb.blue_size, fb.blue_shift);
}

static void pixel(size_t x, size_t y, uint32_t value) {
    volatile uint32_t *line = (volatile uint32_t *)((uintptr_t)fb.address + y * fb.pitch);
    line[x] = value;
}

static void clear_line(size_t line) {
    size_t y0 = MARGIN + line * GLYPH_HEIGHT;
    for (size_t y = y0; y < y0 + GLYPH_HEIGHT; y++)
        for (size_t x = MARGIN; x < MARGIN + columns * GLYPH_WIDTH; x++)
            pixel(x, y, background);
}

static void scroll(void) {
    size_t end_y = MARGIN + (rows - 1) * GLYPH_HEIGHT;
    for (size_t y = MARGIN; y < end_y; y++) {
        volatile uint32_t *source =
            (volatile uint32_t *)((uintptr_t)fb.address + (y + GLYPH_HEIGHT) * fb.pitch);
        volatile uint32_t *destination =
            (volatile uint32_t *)((uintptr_t)fb.address + y * fb.pitch);
        for (size_t x = MARGIN; x < MARGIN + columns * GLYPH_WIDTH; x++)
            destination[x] = source[x];
    }
    clear_line(rows - 1);
    row = rows - 1;
}

static void newline(void) {
    column = 0;
    if (++row >= rows)
        scroll();
}

static void glyph(unsigned char c) {
    if (c >= 128)
        return;
    size_t x0 = MARGIN + column * GLYPH_WIDTH;
    size_t y0 = MARGIN + row * GLYPH_HEIGHT;
    for (size_t y = 0; y < GLYPH_HEIGHT; y++) {
        uint8_t bits = font8x8_basic[c][y / 2];
        for (size_t x = 0; x < GLYPH_WIDTH; x++)
            pixel(x0 + x, y0 + y, bits & (1u << x) ? foreground : background);
    }
}

bool framebuffer_console_init(const struct framebuffer_info *info) {
    ready = false;
    if (!info || !info->address || info->width < 32 || info->height < 48 ||
        info->width > SIZE_MAX / 4 || info->pitch < info->width * 4 || info->pitch & 3 ||
        info->height > SIZE_MAX / info->pitch ||
        (uintptr_t)info->address > UINTPTR_MAX - info->height * info->pitch || !info->red_size ||
        info->red_size > 8 || !info->green_size || info->green_size > 8 || !info->blue_size ||
        info->blue_size > 8 || info->red_shift + info->red_size > 32 ||
        info->green_shift + info->green_size > 32 || info->blue_shift + info->blue_size > 32)
        return false;
    uint32_t red = ((1u << info->red_size) - 1) << info->red_shift;
    uint32_t green = ((1u << info->green_size) - 1) << info->green_shift;
    uint32_t blue = ((1u << info->blue_size) - 1) << info->blue_shift;
    if ((red & green) || (red & blue) || (green & blue))
        return false;
    fb = *info;
    columns = (fb.width - 2 * MARGIN) / GLYPH_WIDTH;
    rows = (fb.height - 2 * MARGIN) / GLYPH_HEIGHT;
    column = row = 0;
    foreground = color(224, 232, 240);
    background = color(16, 24, 32);
    for (size_t y = 0; y < fb.height; y++)
        for (size_t x = 0; x < fb.width; x++)
            pixel(x, y, background);
    ready = true;
    return true;
}

void framebuffer_console_dimensions(size_t *width, size_t *height) {
    *width = columns;
    *height = rows;
}

static void erase_display(unsigned mode) {
    for (size_t y = 0; y < rows; y++)
        for (size_t x = 0; x < columns; x++) {
            bool erase = mode == 2 || (mode == 0 && (y > row || (y == row && x >= column))) ||
                         (mode == 1 && (y < row || (y == row && x <= column)));
            if (erase)
                for (size_t py = 0; py < GLYPH_HEIGHT; py++)
                    for (size_t px = 0; px < GLYPH_WIDTH; px++)
                        pixel(MARGIN + x * GLYPH_WIDTH + px, MARGIN + y * GLYPH_HEIGHT + py,
                              background);
        }
}
static unsigned parameter(unsigned i, unsigned fallback) {
    return i < parameter_count && parameters[i] ? parameters[i] : fallback;
}
static void csi(char c) {
    unsigned n = parameter(0, 1);
    switch (c) {
    case 'H':
    case 'f':
        row = parameter(0, 1) - 1;
        column = parameter(1, 1) - 1;
        break;
    case 'A':
        row = n > row ? 0 : row - n;
        break;
    case 'B':
        row += n;
        break;
    case 'C':
        column += n;
        break;
    case 'D':
        column = n > column ? 0 : column - n;
        break;
    case 'G':
        column = n - 1;
        break;
    case 'd':
        row = n - 1;
        break;
    case 'J':
        erase_display(parameters[0]);
        break;
    case 'K': {
        size_t old = column;
        for (size_t x = 0; x < columns; x++)
            if (parameters[0] == 2 || (parameters[0] == 0 && x >= old) ||
                (parameters[0] == 1 && x <= old)) {
                column = x;
                glyph(' ');
            }
        column = old;
        break;
    }
    case 's':
        saved_row = row;
        saved_column = column;
        break;
    case 'u':
        row = saved_row;
        column = saved_column;
        break;
    case 'm': {
        static const uint8_t palette[8][3] = {{16, 24, 32},   {210, 70, 70},  {80, 200, 100},
                                              {220, 200, 90}, {90, 140, 230}, {200, 100, 210},
                                              {80, 200, 210}, {224, 232, 240}};
        for (unsigned i = 0; i < parameter_count; i++) {
            unsigned p = parameters[i];
            if (p == 0) {
                foreground = color(224, 232, 240);
                background = color(16, 24, 32);
                inverse = false;
            } else if (p == 7 && !inverse) {
                uint32_t t = foreground;
                foreground = background;
                background = t;
                inverse = true;
            } else if (p == 27 && inverse) {
                uint32_t t = foreground;
                foreground = background;
                background = t;
                inverse = false;
            } else if (p >= 30 && p <= 37)
                foreground = color(palette[p - 30][0], palette[p - 30][1], palette[p - 30][2]);
            else if (p >= 40 && p <= 47)
                background = color(palette[p - 40][0], palette[p - 40][1], palette[p - 40][2]);
            else if (p == 39)
                foreground = color(224, 232, 240);
            else if (p == 49)
                background = color(16, 24, 32);
        }
        break;
    }
    }
    if (row >= rows)
        row = rows - 1;
    if (column >= columns)
        column = columns - 1;
}
void framebuffer_console_putc(char c) {
    if (!ready)
        return;
    if (escape_state == 1) {
        escape_state = c == '[' ? 2 : 0;
        parameter_count = 1;
        for (unsigned i = 0; i < 8; i++)
            parameters[i] = 0;
        return;
    }
    if (escape_state == 2) {
        if (c >= '0' && c <= '9') {
            unsigned *p = &parameters[parameter_count - 1];
            if (*p < 10000)
                *p = *p * 10 + c - '0';
            return;
        }
        if (c == ';') {
            if (parameter_count < 8)
                parameter_count++;
            return;
        }
        if (c == '?')
            return;
        csi(c);
        escape_state = 0;
        return;
    }
    if (c == 27) {
        escape_state = 1;
        return;
    }
    if (c == '\n')
        newline();
    else if (c == '\r')
        column = 0;
    else if (c == '\b') {
        if (column) {
            column--;
            glyph(' ');
        }
    } else if (c == '\t') {
        size_t count = 4 - column % 4;
        while (count--)
            framebuffer_console_putc(' ');
    } else if ((unsigned char)c >= 32) {
        if (column == columns)
            newline();
        glyph((unsigned char)c);
        column++;
    }
}
