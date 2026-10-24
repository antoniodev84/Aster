#include "kprintf.h"
#include "framebuffer.h"
#include "hal/hal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

struct output {
    int count;
    bool framebuffer;
};

static unsigned depth;

enum length { DEFAULT, BYTE, SHORT, LONG, LONG_LONG, SIZE, DIFFERENCE, MAXIMUM };

static void emit(struct output *out, char c) {
    hal_debug_putc(c);
    if (out->framebuffer)
        framebuffer_console_putc(c);
    if (out->count < INT_MAX)
        out->count++;
}

static void repeat(struct output *out, char c, unsigned count) {
    while (count--)
        emit(out, c);
}

static unsigned decimal(const char **format) {
    unsigned value = 0;
    while (**format >= '0' && **format <= '9') {
        unsigned digit = (unsigned)(*(*format)++ - '0');
        value = value > (65535u - digit) / 10 ? 65535u : value * 10 + digit;
    }
    return value;
}

static uint64_t get_unsigned(va_list *args, enum length length) {
    switch (length) {
    case BYTE:
        return (unsigned char)va_arg(*args, unsigned int);
    case SHORT:
        return (unsigned short)va_arg(*args, unsigned int);
    case LONG:
        return va_arg(*args, unsigned long);
    case LONG_LONG:
        return va_arg(*args, unsigned long long);
    case SIZE:
        return va_arg(*args, size_t);
    case DIFFERENCE:
        return va_arg(*args, uintptr_t);
    case MAXIMUM:
        return va_arg(*args, uintmax_t);
    default:
        return va_arg(*args, unsigned int);
    }
}

static int64_t get_signed(va_list *args, enum length length) {
    switch (length) {
    case BYTE:
        return (signed char)va_arg(*args, int);
    case SHORT:
        return (short)va_arg(*args, int);
    case LONG:
        return va_arg(*args, long);
    case LONG_LONG:
        return va_arg(*args, long long);
    case SIZE:
    case DIFFERENCE:
        return va_arg(*args, ptrdiff_t);
    case MAXIMUM:
        return va_arg(*args, intmax_t);
    default:
        return va_arg(*args, int);
    }
}

static void integer(struct output *out, uint64_t value, unsigned base, bool uppercase, char sign,
                    bool alternate, bool pointer, unsigned width, int precision, bool left,
                    bool zero) {
    char digits[64];
    unsigned count = 0;
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    uint64_t original = value;
    if (value || precision != 0) {
        do {
            digits[count++] = alphabet[value % base];
            value /= base;
        } while (value);
    }
    unsigned zeros = precision > (int)count ? (unsigned)precision - count : 0;
    const char *prefix = "";
    unsigned prefix_length = 0;
    if (pointer || (alternate && original && base == 16)) {
        prefix = uppercase ? "0X" : "0x";
        prefix_length = 2;
    } else if (alternate && base == 8 && !zeros && (!count || digits[count - 1] != '0')) {
        prefix = "0";
        prefix_length = 1;
    }
    unsigned size = count + zeros + prefix_length + (sign != 0);
    unsigned padding = width > size ? width - size : 0;
    if (!left && (!zero || precision >= 0))
        repeat(out, ' ', padding);
    if (sign)
        emit(out, sign);
    for (unsigned i = 0; i < prefix_length; i++)
        emit(out, prefix[i]);
    if (!left && zero && precision < 0)
        repeat(out, '0', padding);
    repeat(out, '0', zeros);
    while (count)
        emit(out, digits[--count]);
    if (left)
        repeat(out, ' ', padding);
}

int kvprintf(const char *format, va_list arguments) {
    if (!format)
        return -1;
    uint64_t state = hal_irq_save();
    struct output out = {.framebuffer = depth++ == 0};
    va_list args;
    va_copy(args, arguments);
    bool valid = true;
    while (*format && valid) {
        if (*format != '%') {
            emit(&out, *format++);
            continue;
        }
        format++;
        bool left = false, zero = false, alternate = false, plus = false, space = false;
        bool flags = true;
        while (flags) {
            switch (*format) {
            case '-':
                left = true;
                format++;
                break;
            case '0':
                zero = true;
                format++;
                break;
            case '#':
                alternate = true;
                format++;
                break;
            case '+':
                plus = true;
                format++;
                break;
            case ' ':
                space = true;
                format++;
                break;
            default:
                flags = false;
                break;
            }
        }
        unsigned width;
        if (*format == '*') {
            int argument = va_arg(args, int);
            left |= argument < 0;
            uint64_t magnitude = argument < 0 ? -(int64_t)argument : argument;
            width = magnitude > 65535 ? 65535 : (unsigned)magnitude;
            format++;
        } else
            width = decimal(&format);
        int precision = -1;
        if (*format == '.') {
            format++;
            if (*format == '*') {
                precision = va_arg(args, int);
                if (precision < 0)
                    precision = -1;
                if (precision > 65535)
                    precision = 65535;
                format++;
            } else
                precision = (int)decimal(&format);
        }
        enum length length = DEFAULT;
        if (*format == 'h') {
            length = SHORT;
            if (*++format == 'h') {
                length = BYTE;
                format++;
            }
        } else if (*format == 'l') {
            length = LONG;
            if (*++format == 'l') {
                length = LONG_LONG;
                format++;
            }
        } else if (*format == 'z') {
            length = SIZE;
            format++;
        } else if (*format == 't') {
            length = DIFFERENCE;
            format++;
        } else if (*format == 'j') {
            length = MAXIMUM;
            format++;
        }
        char specifier = *format;
        if (*format)
            format++;
        switch (specifier) {
        case 's': {
            const char *s = va_arg(args, const char *);
            if (!s)
                s = "(null)";
            unsigned count = 0;
            while ((precision < 0 || count < (unsigned)precision) && s[count])
                count++;
            unsigned padding = width > count ? width - count : 0;
            if (!left)
                repeat(&out, ' ', padding);
            for (unsigned i = 0; i < count; i++)
                emit(&out, s[i]);
            if (left)
                repeat(&out, ' ', padding);
            break;
        }
        case 'c':
            if (!left && width > 1)
                repeat(&out, ' ', width - 1);
            emit(&out, (char)va_arg(args, int));
            if (left && width > 1)
                repeat(&out, ' ', width - 1);
            break;
        case '%':
            emit(&out, '%');
            break;
        case 'd':
        case 'i': {
            int64_t value = get_signed(&args, length);
            char sign = value < 0 ? '-' : plus ? '+' : space ? ' ' : 0;
            uint64_t magnitude = value < 0 ? 0ull - (uint64_t)value : (uint64_t)value;
            integer(&out, magnitude, 10, false, sign, false, false, width, precision, left, zero);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        case 'p': {
            uint64_t value =
                specifier == 'p' ? (uintptr_t)va_arg(args, void *) : get_unsigned(&args, length);
            unsigned base = specifier == 'u' ? 10 : specifier == 'o' ? 8 : 16;
            integer(&out, value, base, specifier == 'X', 0, alternate, specifier == 'p', width,
                    precision, left, zero);
            break;
        }
        default:
            valid = false;
            break;
        }
    }
    va_end(args);
    depth--;
    hal_irq_restore(state);
    return valid ? out.count : -1;
}

int kprintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int count = kvprintf(format, args);
    va_end(args);
    return count;
}
