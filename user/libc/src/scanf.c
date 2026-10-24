#include <os.h>
#include <limits.h>

struct scan_input {
    FILE *stream;
    const char *text;
    size_t position, consumed;
    int pushed;
};
static int next(struct scan_input *in) {
    int c;
    if (in->pushed >= 0) {
        c = in->pushed;
        in->pushed = -1;
    } else if (in->stream)
        c = fgetc(in->stream);
    else
        c = in->text[in->position] ? (unsigned char)in->text[in->position++] : EOF;
    if (c != EOF)
        in->consumed++;
    return c;
}
static void back(struct scan_input *in, int c) {
    if (c != EOF) {
        in->pushed = c;
        in->consumed--;
    }
}
static int scan(struct scan_input *in, const char *format, va_list args) {
    int assigned = 0;
    bool failed_input = false;
    while (*format) {
        if (isspace((unsigned char)*format)) {
            while (isspace((unsigned char)*format))
                format++;
            int c;
            while (isspace(c = next(in))) {
            }
            back(in, c);
            continue;
        }
        if (*format != '%') {
            int c = next(in);
            if (c != (unsigned char)*format) {
                if (c == EOF)
                    failed_input = true;
                back(in, c);
                break;
            }
            format++;
            continue;
        }
        format++;
        if (*format == '%') {
            int c = next(in);
            if (c != '%') {
                if (c == EOF)
                    failed_input = true;
                back(in, c);
                break;
            }
            format++;
            continue;
        }
        bool suppress = false;
        if (*format == '*') {
            suppress = true;
            format++;
        }
        unsigned width = 0;
        while (isdigit(*format)) {
            if (width < 65536)
                width = width * 10 + *format - '0';
            format++;
        }
        if (!width)
            width = 65536;
        int length = 0;
        if (*format == 'h') {
            format++;
            length = -1;
            if (*format == 'h') {
                length = -2;
                format++;
            }
        } else if (*format == 'l') {
            format++;
            length = 1;
            if (*format == 'l') {
                length = 2;
                format++;
            }
        } else if (*format == 'z' || *format == 't' || *format == 'j') {
            length = 2;
            format++;
        } else if (*format == 'L') {
            length = 3;
            format++;
        }
        char spec = *format++;
        if (!spec)
            break;
        if (spec == 'n') {
            if (!suppress) {
                void *p = va_arg(args, void *);
                if (length == -2)
                    *(signed char *)p = in->consumed;
                else if (length == -1)
                    *(short *)p = in->consumed;
                else if (length == 1)
                    *(long *)p = in->consumed;
                else if (length == 2)
                    *(long long *)p = in->consumed;
                else
                    *(int *)p = in->consumed;
            }
            continue;
        }
        if (spec != 'c' && spec != '[') {
            int c;
            while (isspace(c = next(in))) {
            }
            back(in, c);
        }
        if (spec == 'c' || spec == 's' || spec == '[') {
            bool table[256] = {0}, invert = false;
            if (spec == '[') {
                if (*format == '^') {
                    invert = true;
                    format++;
                }
                if (*format == ']') {
                    table[']'] = true;
                    format++;
                }
                while (*format && *format != ']') {
                    unsigned char a = *format++, b = a;
                    if (*format == '-' && format[1] && format[1] != ']') {
                        format++;
                        b = *format++;
                    }
                    if (a <= b)
                        for (unsigned v = a; v <= b; v++)
                            table[v] = true;
                }
                if (*format != ']')
                    break;
                format++;
            }
            if (spec == 'c' && width == 65536)
                width = 1;
            char *destination = suppress ? NULL : va_arg(args, char *);
            unsigned used = 0;
            while (used < width) {
                int c = next(in);
                if (c == EOF) {
                    failed_input = true;
                    break;
                }
                bool accept = spec == 'c' ||
                              (spec == 's' ? !isspace(c) : (table[(unsigned char)c] != invert));
                if (!accept) {
                    back(in, c);
                    break;
                }
                if (destination)
                    destination[used] = c;
                used++;
            }
            if (!used || (spec == 'c' && used != width))
                break;
            if (destination && spec != 'c')
                destination[used] = 0;
            if (!suppress)
                assigned++;
            continue;
        }
        bool numeric = spec == 'd' || spec == 'i' || spec == 'u' || spec == 'o' || spec == 'x' ||
                       spec == 'X' || spec == 'p',
             floating = strchr("aAeEfFgG", spec) != NULL;
        if (!numeric && !floating)
            break;
        char token[4097];
        unsigned used = 0;
        if (width > 4096)
            width = 4096;
        int c = next(in);
        if (c == '+' || c == '-') {
            token[used++] = c;
            c = next(in);
        }
        bool seen_dot = false, seen_exponent = false;
        unsigned base = spec == 'o' ? 8 : spec == 'x' || spec == 'X' || spec == 'p' ? 16 : 10;
        while (used < width && c != EOF) {
            bool accept = false;
            if (numeric) {
                if (spec == 'i')
                    accept = isdigit(c) ||
                             (used > 0 && token[used - 1] == '0' && (c == 'x' || c == 'X')) ||
                             (used > 1 && (token[used - 2] == 'x' || token[used - 2] == 'X') &&
                              isxdigit(c));
                else
                    accept = (base == 16  ? isxdigit(c)
                              : base == 8 ? c >= '0' && c <= '7'
                                          : isdigit(c)) ||
                             ((c == 'x' || c == 'X') && used > 0 && token[used - 1] == '0' &&
                              base == 16);
            } else {
                accept = isdigit(c) || isalpha(c);
                if (c == '.' && !seen_dot) {
                    seen_dot = true;
                    accept = true;
                }
                if (c == 'e' || c == 'E' || c == 'p' || c == 'P')
                    seen_exponent = true;
                if ((c == '+' || c == '-') && seen_exponent && used &&
                    (token[used - 1] == 'e' || token[used - 1] == 'E' || token[used - 1] == 'p' ||
                     token[used - 1] == 'P'))
                    accept = true;
            }
            if (!accept)
                break;
            token[used++] = c;
            c = next(in);
        }
        back(in, c);
        token[used] = 0;
        if (!used) {
            if (c == EOF)
                failed_input = true;
            break;
        }
        char *end;
        if (floating) {
            double value = strtod(token, &end);
            if (end == token)
                break;
            if (!suppress) {
                void *destination = va_arg(args, void *);
                if (length == 3)
                    *(long double *)destination = value;
                else if (length == 1)
                    *(double *)destination = value;
                else
                    *(float *)destination = value;
                assigned++;
            }
        } else {
            unsigned realbase = spec == 'i' ? 0 : base;
            unsigned long long value = spec == 'd' || spec == 'i'
                                           ? (unsigned long long)strtoll(token, &end, realbase)
                                           : strtoull(token, &end, realbase);
            if (end == token)
                break;
            if (!suppress) {
                void *p = va_arg(args, void *);
                if (spec == 'p')
                    *(void **)p = (void *)(uintptr_t)value;
                else if (length == -2)
                    *(unsigned char *)p = value;
                else if (length == -1)
                    *(unsigned short *)p = value;
                else if (length == 1)
                    *(unsigned long *)p = value;
                else if (length == 2)
                    *(unsigned long long *)p = value;
                else
                    *(unsigned *)p = value;
                assigned++;
            }
        }
    }
    if (in->stream && in->pushed >= 0)
        ungetc(in->pushed, in->stream);
    return !assigned && failed_input ? EOF : assigned;
}
int vsscanf(const char *text, const char *format, va_list args) {
    struct scan_input in = {.text = text, .pushed = -1};
    va_list copy;
    va_copy(copy, args);
    int n = scan(&in, format, copy);
    va_end(copy);
    return n;
}
int sscanf(const char *text, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsscanf(text, format, args);
    va_end(args);
    return n;
}
int vfscanf(FILE *stream, const char *format, va_list args) {
    struct scan_input in = {.stream = stream, .pushed = -1};
    va_list copy;
    va_copy(copy, args);
    int n = scan(&in, format, copy);
    va_end(copy);
    return n;
}
int fscanf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vfscanf(stream, format, args);
    va_end(args);
    return n;
}
int scanf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vfscanf(stdin, format, args);
    va_end(args);
    return n;
}
