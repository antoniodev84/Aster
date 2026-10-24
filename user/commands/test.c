#include "common.h"
static int count;
static char **words;
static int at;
static bool expression(void);
static bool atom(void) {
    if (at >= count)
        return false;
    if (!strcmp(words[at], "!")) {
        at++;
        return !atom();
    }
    if (!strcmp(words[at], "(")) {
        at++;
        bool value = expression();
        if (at < count && !strcmp(words[at], ")"))
            at++;
        return value;
    }
    const char *a = words[at++];
    if (at < count && a[0] == '-' && a[1] && !a[2]) {
        const char *b = words[at++];
        if (a[1] == 'n')
            return *b;
        if (a[1] == 'z')
            return !*b;
        struct stat s;
        if (a[1] == 'r' || a[1] == 'w' || a[1] == 'x')
            return access(b, a[1] == 'r' ? R_OK : a[1] == 'w' ? W_OK : X_OK) == 0;
        if (lstat(b, &s) < 0)
            return false;
        switch (a[1]) {
        case 'e':
            return true;
        case 'f':
            return S_ISREG(s.st_mode);
        case 'd':
            return S_ISDIR(s.st_mode);
        case 'L':
        case 'h':
            return S_ISLNK(s.st_mode);
        case 's':
            return s.st_size > 0;
        default:
            return false;
        }
    }
    if (at + 1 < count) {
        const char *op = words[at];
        if (!strcmp(op, "=") || !strcmp(op, "==") || !strcmp(op, "!=")) {
            at += 2;
            bool same = !strcmp(a, words[at - 1]);
            return op[0] == '!' ? !same : same;
        }
        if (!strcmp(op, "-eq") || !strcmp(op, "-ne") || !strcmp(op, "-lt") || !strcmp(op, "-le") ||
            !strcmp(op, "-gt") || !strcmp(op, "-ge")) {
            long x = strtol(a, NULL, 10), y = strtol(words[at + 1], NULL, 10);
            at += 2;
            if (op[1] == 'e')
                return x == y;
            if (op[1] == 'n')
                return x != y;
            if (op[1] == 'l')
                return op[2] == 't' ? x < y : x <= y;
            return op[2] == 't' ? x > y : x >= y;
        }
    }
    return *a;
}
static bool conjunction(void) {
    bool value = atom();
    while (at < count && !strcmp(words[at], "-a")) {
        at++;
        bool b = atom();
        value = value && b;
    }
    return value;
}
static bool expression(void) {
    bool value = conjunction();
    while (at < count && !strcmp(words[at], "-o")) {
        at++;
        bool b = conjunction();
        value = value || b;
    }
    return value;
}
int main(int argc, char **argv) {
    count = argc - 1;
    words = argv + 1;
    if (!count)
        return 1;
    bool value = expression();
    if (at != count)
        return 2;
    return !value;
}
