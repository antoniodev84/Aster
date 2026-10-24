#include <os.h>

static unsigned decimal_at(const char *digits, int count, int index) {
    return index >= 0 && index < count ? (unsigned)(digits[index] - '0') : 0;
}
static bool should_round(const char *digits, int count, int cut, int previous) {
    unsigned digit = decimal_at(digits, count, cut);
    if (digit > 5)
        return true;
    if (digit < 5)
        return false;
    for (int i = cut + 1; i < count; i++)
        if (decimal_at(digits, count, i))
            return true;
    return previous & 1;
}
static size_t round_output(char *buffer, size_t length, bool round) {
    if (!round)
        return length;
    size_t i = length;
    while (i) {
        i--;
        if (buffer[i] == '.')
            continue;
        if (buffer[i] < '9') {
            buffer[i]++;
            return length;
        }
        buffer[i] = '0';
    }
    memmove(buffer + 1, buffer, length);
    buffer[0] = '1';
    return length + 1;
}
int __format_float(char *output, size_t capacity, double value, char specifier, int precision,
                   bool alternate) {
    if (capacity < 1500) {
        errno = EINVAL;
        return -1;
    }
    union {
        double d;
        uint64_t u;
    } bits = {value};
    bits.u &= ~(1ull << 63);
    bool uppercase = specifier >= 'A' && specifier <= 'Z';
    char kind = tolower(specifier);
    unsigned exponent = (bits.u >> 52) & 2047;
    uint64_t fraction = bits.u & 0xfffffffffffffull;
    if (exponent == 2047) {
        strcpy(output, fraction ? (uppercase ? "NAN" : "nan") : (uppercase ? "INF" : "inf"));
        return 3;
    }
    if (kind == 'a') {
        unsigned requested = precision < 0 ? 13 : (unsigned)precision;
        if (requested > 1000) {
            errno = EOVERFLOW;
            return -1;
        }
        int power = exponent ? (int)exponent - 1023 : -1022;
        unsigned leading = exponent ? 1 : 0;
        if (!exponent && !fraction)
            power = 0;
        if (requested < 13) {
            unsigned shift = (13 - requested) * 4;
            uint64_t lost = fraction & ((1ull << shift) - 1), half = 1ull << (shift - 1);
            uint64_t kept = fraction >> shift;
            if (lost > half || (lost == half && ((requested ? kept : leading) & 1))) {
                kept++;
                if (kept >= (1ull << (requested * 4))) {
                    leading++;
                    kept = 0;
                }
            }
            fraction = kept << shift;
            if (leading == 2) {
                leading = 1;
                power++;
            }
        }
        const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
        size_t n = 0;
        output[n++] = '0';
        output[n++] = uppercase ? 'X' : 'x';
        output[n++] = '0' + leading;
        if (requested || alternate)
            output[n++] = '.';
        for (unsigned i = 0; i < requested; i++)
            output[n++] = i < 13 ? alphabet[(fraction >> (48 - i * 4)) & 15] : '0';
        if (precision < 0 && !alternate) {
            while (n && output[n - 1] == '0')
                n--;
            if (n && output[n - 1] == '.')
                n--;
        }
        output[n++] = uppercase ? 'P' : 'p';
        output[n++] = power < 0 ? '-' : '+';
        if (power < 0)
            power = -power;
        char digits[12];
        unsigned c = 0;
        do {
            digits[c++] = '0' + power % 10;
            power /= 10;
        } while (power);
        while (c)
            output[n++] = digits[--c];
        output[n] = 0;
        return n;
    }
    unsigned char little[1200] = {0};
    int count = 0;
    uint64_t mantissa = fraction | (exponent ? 1ull << 52 : 0);
    int binary_power = exponent ? (int)exponent - 1023 - 52 : -1074;
    do {
        little[count++] = mantissa % 10;
        mantissa /= 10;
    } while (mantissa);
    int scale = binary_power < 0 ? -binary_power : 0;
    unsigned factor = binary_power < 0 ? 5 : 2;
    unsigned times = binary_power < 0 ? -binary_power : binary_power;
    for (unsigned step = 0; step < times; step++) {
        unsigned carry = 0;
        for (int i = 0; i < count; i++) {
            unsigned product = little[i] * factor + carry;
            little[i] = product % 10;
            carry = product / 10;
        }
        while (carry) {
            if (count >= 1200) {
                errno = EOVERFLOW;
                return -1;
            }
            little[count++] = carry % 10;
            carry /= 10;
        }
    }
    char digits[1200];
    for (int i = 0; i < count; i++)
        digits[i] = '0' + little[count - 1 - i];
    int point = count - scale;
    if (!fraction && !exponent) {
        digits[0] = '0';
        count = 1;
        point = 1;
    }
    if (precision < 0)
        precision = 6;
    if (precision > 1000) {
        errno = EOVERFLOW;
        return -1;
    }
    if (kind == 'g' && !precision)
        precision = 1;
    bool scientific = kind == 'e' || (kind == 'g' && (point - 1 < -4 || point - 1 >= precision));
    int decimals = kind == 'g' ? (scientific ? precision - 1 : precision - point) : precision;
    if (decimals < 0)
        decimals = 0;
    size_t n = 0;
    int cut;
    if (scientific) {
        output[n++] = digits[0];
        if (decimals || alternate)
            output[n++] = '.';
        for (int i = 0; i < decimals; i++)
            output[n++] = '0' + decimal_at(digits, count, i + 1);
        cut = decimals + 1;
    } else {
        int integers = point > 0 ? point : 1;
        for (int i = 0; i < integers; i++)
            output[n++] = '0' + (point > 0 ? decimal_at(digits, count, i) : 0);
        if (decimals || alternate)
            output[n++] = '.';
        for (int i = 0; i < decimals; i++)
            output[n++] = '0' + decimal_at(digits, count, point + i);
        cut = point + decimals;
    }
    int previous = n && output[n - 1] != '.' ? output[n - 1] - '0'
                   : n > 1                   ? output[n - 2] - '0'
                                             : 0;
    size_t old = n;
    n = round_output(output, n, should_round(digits, count, cut, previous));
    if (scientific && n > old) {
        memmove(output + 1, output + 2, n - 2);
        n--;
        point++;
    }
    if (kind == 'g') {
        int rounded_point = point + (!scientific && n > old ? 1 : 0);
        bool choose_scientific = rounded_point - 1 < -4 || rounded_point - 1 >= precision;
        if (choose_scientific != scientific) {
            int places = choose_scientific ? precision - 1 : precision - rounded_point;
            if (places < 0)
                places = 0;
            int result = __format_float(output, capacity, value,
                                        choose_scientific ? (uppercase ? 'E' : 'e')
                                                          : (uppercase ? 'F' : 'f'),
                                        places, alternate);
            if (result < 0)
                return result;
            if (!alternate) {
                char *exponent_position = strchr(output, uppercase ? 'E' : 'e');
                size_t end =
                    exponent_position ? (size_t)(exponent_position - output) : (size_t)result;
                size_t trim = end;
                if (memchr(output, '.', end)) {
                    while (trim && output[trim - 1] == '0')
                        trim--;
                    if (trim && output[trim - 1] == '.')
                        trim--;
                }
                memmove(output + trim, output + end, (size_t)result - end + 1);
                result -= end - trim;
            }
            return result;
        }
    }
    if (kind == 'g' && !alternate) {
        while (n && output[n - 1] == '0' && memchr(output, '.', n))
            n--;
        if (n && output[n - 1] == '.')
            n--;
    }
    if (scientific) {
        int decimal_exponent = point - 1;
        output[n++] = uppercase ? 'E' : 'e';
        output[n++] = decimal_exponent < 0 ? '-' : '+';
        if (decimal_exponent < 0)
            decimal_exponent = -decimal_exponent;
        char reversed[12];
        unsigned used = 0;
        do {
            reversed[used++] = '0' + decimal_exponent % 10;
            decimal_exponent /= 10;
        } while (decimal_exponent);
        while (used < 2)
            reversed[used++] = '0';
        while (used)
            output[n++] = reversed[--used];
    }
    output[n] = 0;
    return n;
}
