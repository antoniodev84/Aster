#include <os.h>
#include <limits.h>

struct output {
    int count;
    bool failed;
    FILE *stream;
    char *buffer;
    size_t capacity;
};

enum length { DEFAULT, BYTE, SHORT, LONG, LONG_LONG, SIZE, DIFFERENCE, MAXIMUM, EXTENDED };
extern int __format_float(char *, size_t, double, char, int, bool);

static void emit(struct output *out, char c) {
    if (out->stream && fputc((unsigned char)c, out->stream) == EOF)
        out->failed = true;
    if (out->buffer && out->capacity && (size_t)out->count < out->capacity - 1)
        out->buffer[out->count] = c;
    if (out->count < INT_MAX)
        out->count++;
    else {
        out->failed = true;
        errno = EOVERFLOW;
    }
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

static int format_output(struct output *destination, const char *format, va_list arguments) {
    if (!format)
        return -1;
    struct output out = *destination;
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
        } else if (*format == 'L') {
            length = EXTENDED;
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
        case 'n': {
            void *pointer = va_arg(args, void *);
            if (length == BYTE)
                *(signed char *)pointer = out.count;
            else if (length == SHORT)
                *(short *)pointer = out.count;
            else if (length == LONG)
                *(long *)pointer = out.count;
            else if (length == LONG_LONG)
                *(long long *)pointer = out.count;
            else if (length == SIZE)
                *(size_t *)pointer = out.count;
            else if (length == DIFFERENCE)
                *(ptrdiff_t *)pointer = out.count;
            else if (length == MAXIMUM)
                *(intmax_t *)pointer = out.count;
            else
                *(int *)pointer = out.count;
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            if (length == EXTENDED) {
                valid = false;
                break;
            }
            double value = va_arg(args, double);
            union {
                double d;
                uint64_t bits;
            } representation = {value};
            char sign = (representation.bits >> 63) ? '-' : plus ? '+' : space ? ' ' : 0;
            char text[1600];
            int length = __format_float(text, sizeof(text), value, specifier, precision, alternate);
            if (length < 0) {
                valid = false;
                break;
            }
            if (tolower(text[0]) == 'i' || tolower(text[0]) == 'n')
                zero = false;
            unsigned size = (unsigned)length + (sign != 0),
                     padding = width > size ? width - size : 0;
            if (!left && !zero)
                repeat(&out, ' ', padding);
            if (sign)
                emit(&out, sign);
            size_t start = 0;
            if (!left && zero) {
                if (length > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
                    emit(&out, text[0]);
                    emit(&out, text[1]);
                    start = 2;
                }
                repeat(&out, '0', padding);
            }
            for (size_t i = start; i < (size_t)length; i++)
                emit(&out, text[i]);
            if (left)
                repeat(&out, ' ', padding);
            break;
        }
        default:
            valid = false;
            break;
        }
    }
    va_end(args);
    *destination = out;
    if (out.buffer && out.capacity) {
        size_t at = (size_t)out.count < out.capacity ? (size_t)out.count : out.capacity - 1;
        out.buffer[at] = 0;
    }
    if (!valid) {
        errno = EINVAL;
        return -1;
    }
    return out.failed ? -1 : out.count;
}

int vfprintf(FILE *stream, const char *format, va_list args) {
    struct output out = {.stream = stream};
    return format_output(&out, format, args);
}
int vsnprintf(char *buffer, size_t size, const char *format, va_list args) {
    struct output out = {.buffer = buffer, .capacity = size};
    return format_output(&out, format, args);
}
int fprintf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vfprintf(stream, format, args);
    va_end(args);
    return n;
}
int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vfprintf(stdout, format, args);
    va_end(args);
    return n;
}
int vprintf(const char *format, va_list args) {
    return vfprintf(stdout, format, args);
}
int snprintf(char *buffer, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buffer, size, format, args);
    va_end(args);
    return n;
}
int vsprintf(char *buffer, const char *format, va_list args) {
    return vsnprintf(buffer, SIZE_MAX, format, args);
}
int sprintf(char *buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsprintf(buffer, format, args);
    va_end(args);
    return n;
}
int vasprintf(char **buffer, const char *format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (n < 0)
        return -1;
    *buffer = malloc((size_t)n + 1);
    if (!*buffer)
        return -1;
    return vsnprintf(*buffer, (size_t)n + 1, format, args);
}
int asprintf(char **buffer, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vasprintf(buffer, format, args);
    va_end(args);
    return n;
}
