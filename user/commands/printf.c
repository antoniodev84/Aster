#include "common.h"

static int escape(const char **pointer) {
    const char *p = *pointer;
    int c = (unsigned char)*p++;
    switch (c) {
    case 'a':
        c = 7;
        break;
    case 'b':
        c = '\b';
        break;
    case 'f':
        c = '\f';
        break;
    case 'n':
        c = '\n';
        break;
    case 'r':
        c = '\r';
        break;
    case 't':
        c = '\t';
        break;
    case 'v':
        c = '\v';
        break;
    case '\\':
        c = '\\';
        break;
    default:
        if (c >= '0' && c <= '7') {
            unsigned value = c - '0';
            for (unsigned i = 0; i < 2 && *p >= '0' && *p <= '7'; i++)
                value = value * 8 + *p++ - '0';
            c = value & 255;
        } else {
            putchar('\\');
        }
        break;
    }
    *pointer = p;
    return c;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("usage: printf FORMAT [ARGUMENT...]\n", stderr);
        return 2;
    }
    int argument = 2;
    bool conversion;
    do {
        conversion = false;
        const char *p = argv[1];
        while (*p) {
            if (*p == '\\' && p[1]) {
                p++;
                putchar(escape(&p));
                continue;
            }
            if (*p++ != '%') {
                putchar(p[-1]);
                continue;
            }
            if (*p == '%') {
                putchar('%');
                p++;
                continue;
            }
            conversion = true;
            char format[256] = "%";
            size_t used = 1;
            while (*p && strchr("-+ #0.0123456789", *p)) {
                if (used + 4 >= sizeof(format)) {
                    fputs("printf: format too long\n", stderr);
                    return 2;
                }
                format[used++] = *p++;
            }
            while (*p && strchr("hljztL", *p))
                p++;
            char specifier = *p++;
            if (!specifier || !strchr("diuoxXscbfFeEgGaA", specifier)) {
                fputs("printf: unsupported conversion\n", stderr);
                return 2;
            }
            const char *value = argument < argc ? argv[argument++] : "";
            if (strchr("diuoxX", specifier)) {
                format[used++] = 'l';
                format[used++] = 'l';
                format[used++] = specifier;
                format[used] = 0;
                long long number = *value == '\'' || *value == '"' ? (unsigned char)value[1]
                                                                   : strtoll(value, NULL, 0);
                if (specifier == 'd' || specifier == 'i')
                    printf(format, number);
                else
                    printf(format, (unsigned long long)number);
            } else if (specifier == 'b') {
                while (*value) {
                    if (*value == '\\' && value[1]) {
                        value++;
                        if (*value == 'c')
                            return ferror(stdout) ? 1 : 0;
                        if (*value == '0') {
                            value++;
                            unsigned octal = 0;
                            for (unsigned i = 0; i < 3 && *value >= '0' && *value <= '7'; i++)
                                octal = octal * 8 + *value++ - '0';
                            putchar(octal & 255);
                        } else
                            putchar(escape(&value));
                    } else
                        putchar(*value++);
                }
            } else {
                format[used++] = specifier;
                format[used] = 0;
                if (specifier == 's')
                    printf(format, value);
                else if (specifier == 'c')
                    printf(format, (unsigned char)*value);
                else
                    printf(format, strtod(value, NULL));
            }
        }
    } while (conversion && argument < argc);
    return ferror(stdout) ? 1 : 0;
}
