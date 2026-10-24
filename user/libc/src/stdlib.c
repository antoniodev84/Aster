#include <os.h>
#include <limits.h>

static unsigned digit(int c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    c = tolower(c);
    return c >= 'a' && c <= 'z' ? (unsigned)(c - 'a' + 10) : 36;
}
static unsigned long long parse_integer(const char *text, char **end, int base, bool sign_limit,
                                        bool *negative) {
    const char *start = text;
    while (isspace((unsigned char)*text))
        text++;
    *negative = false;
    if (*text == '+' || *text == '-')
        *negative = *text++ == '-';
    if (base && (base < 2 || base > 36)) {
        errno = EINVAL;
        if (end)
            *end = (char *)start;
        return 0;
    }
    if ((!base || base == 16) && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') &&
        digit(text[2]) < 16) {
        base = 16;
        text += 2;
    }
    if (!base)
        base = *text == '0' ? 8 : 10;
    unsigned long long limit = sign_limit
                                   ? (*negative ? 9223372036854775808ull : 9223372036854775807ull)
                                   : ULLONG_MAX,
                       value = 0;
    bool any = false, overflow = false;
    while (digit(*text) < (unsigned)base) {
        unsigned d = digit(*text++);
        any = true;
        if (value > (limit - d) / (unsigned)base)
            overflow = true;
        else if (!overflow)
            value = value * base + d;
    }
    if (end)
        *end = (char *)(any ? text : start);
    if (overflow) {
        errno = ERANGE;
        return limit;
    }
    return value;
}
long long strtoll(const char *s, char **e, int b) {
    bool negative;
    unsigned long long n = parse_integer(s, e, b, true, &negative);
    return negative ? (long long)(0ull - n) : (long long)n;
}
unsigned long long strtoull(const char *s, char **e, int b) {
    bool negative;
    int saved = errno;
    errno = 0;
    unsigned long long n = parse_integer(s, e, b, false, &negative);
    int error = errno;
    if (!error)
        errno = saved;
    return negative && error != ERANGE ? 0ull - n : n;
}
long strtol(const char *s, char **e, int b) {
    return strtoll(s, e, b);
}
unsigned long strtoul(const char *s, char **e, int b) {
    return strtoull(s, e, b);
}
int atoi(const char *s) {
    return strtol(s, NULL, 10);
}
long atol(const char *s) {
    return strtol(s, NULL, 10);
}
long long atoll(const char *s) {
    return strtoll(s, NULL, 10);
}
double strtod(const char *s, char **end) {
    const char *start = s;
    while (isspace((unsigned char)*s))
        s++;
    bool negative = false;
    if (*s == '-' || *s == '+')
        negative = *s++ == '-';
    if (!strncasecmp(s, "inf", 3)) {
        s += 3;
        if (!strncasecmp(s, "inity", 5))
            s += 5;
        if (end)
            *end = (char *)s;
        union {
            uint64_t u;
            double d;
        } v = {.u = 0x7ff0000000000000ull | (negative ? 1ull << 63 : 0)};
        return v.d;
    }
    if (!strncasecmp(s, "nan", 3)) {
        if (end)
            *end = (char *)(s + 3);
        union {
            uint64_t u;
            double d;
        } v = {.u = 0x7ff8000000000000ull};
        return v.d;
    }
    bool hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    if (hex)
        s += 2;
    unsigned base = hex ? 16 : 10;
    double value = 0;
    bool any = false;
    int fraction = 0;
    while (digit(*s) < base) {
        value = value * base + digit(*s++);
        any = true;
    }
    if (*s == '.') {
        s++;
        while (digit(*s) < base) {
            value = value * base + digit(*s++);
            fraction++;
            any = true;
        }
    }
    if (!any) {
        if (end)
            *end = (char *)start;
        return 0;
    }
    int exponent = 0;
    char marker = hex ? 'p' : 'e';
    if (tolower(*s) == marker) {
        const char *mark = s++;
        bool minus = false;
        if (*s == '+' || *s == '-')
            minus = *s++ == '-';
        if (!isdigit(*s))
            s = mark;
        else {
            while (isdigit(*s)) {
                if (exponent < 10000)
                    exponent = exponent * 10 + *s - '0';
                s++;
            }
            if (minus)
                exponent = -exponent;
        }
    }
    exponent -= fraction * (hex ? 4 : 1);
    double power = hex ? 2.0 : 10.0;
    if (exponent > 4096) {
        exponent = 4096;
        errno = ERANGE;
    }
    if (exponent < -4096) {
        exponent = -4096;
        errno = ERANGE;
    }
    while (exponent > 0) {
        value *= power;
        exponent--;
    }
    while (exponent < 0) {
        value /= power;
        exponent++;
    }
    if (end)
        *end = (char *)s;
    union {
        double d;
        uint64_t u;
    } v = {value};
    if ((v.u & 0x7ff0000000000000ull) == 0x7ff0000000000000ull)
        errno = ERANGE;
    return negative ? -value : value;
}
float strtof(const char *s, char **e) {
    return strtod(s, e);
}
long double strtold(const char *s, char **e) {
    return strtod(s, e);
}
double atof(const char *s) {
    return strtod(s, NULL);
}
int abs(int n) {
    return n < 0 ? -n : n;
}
long labs(long n) {
    return n < 0 ? -n : n;
}
long long llabs(long long n) {
    return n < 0 ? -n : n;
}
div_t div(int n, int d) {
    return (div_t){n / d, n % d};
}
ldiv_t ldiv(long n, long d) {
    return (ldiv_t){n / d, n % d};
}
lldiv_t lldiv(long long n, long long d) {
    return (lldiv_t){n / d, n % d};
}
static void swap_bytes(char *a, char *b, size_t n) {
    while (n--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}
static void sift(char *base, size_t root, size_t count, size_t size,
                 int (*compare)(const void *, const void *)) {
    while (root < count / 2) {
        size_t child = root * 2 + 1;
        if (child + 1 < count && compare(base + child * size, base + (child + 1) * size) < 0)
            child++;
        if (compare(base + root * size, base + child * size) >= 0)
            return;
        swap_bytes(base + root * size, base + child * size, size);
        root = child;
    }
}
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *)) {
    if (!size || count < 2)
        return;
    if (count > SIZE_MAX / size) {
        errno = EINVAL;
        return;
    }
    for (size_t i = count / 2; i; i--)
        sift(base, i - 1, count, size, compare);
    for (size_t i = count - 1; i; i--) {
        swap_bytes(base, (char *)base + i * size, size);
        sift(base, 0, i, size, compare);
    }
}
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *)) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const char *p = (const char *)base + mid * size;
        int c = compare(key, p);
        if (!c)
            return (void *)p;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}
static bool owned_environment;
static size_t environment_capacity;
struct env_owned {
    char *text;
    struct env_owned *next;
};
static struct env_owned *owned;
static void release_env(char *text) {
    struct env_owned **link = &owned;
    while (*link && (*link)->text != text)
        link = &(*link)->next;
    if (*link) {
        struct env_owned *entry = *link;
        *link = entry->next;
        free(entry->text);
        free(entry);
    }
}
char *getenv(const char *name) {
    size_t n = strlen(name);
    if (!n || strchr(name, '='))
        return NULL;
    for (size_t i = 0; environ && environ[i]; i++)
        if (!strncmp(environ[i], name, n) && environ[i][n] == '=')
            return environ[i] + n + 1;
    return NULL;
}
static int environment_insert(char *entry) {
    size_t n = strcspn(entry, "="), count = 0;
    if (!n || entry[n] != '=') {
        errno = EINVAL;
        return -1;
    }
    while (environ && environ[count]) {
        if (!strncmp(environ[count], entry, n) && environ[count][n] == '=') {
            if (environ[count] != entry)
                release_env(environ[count]);
            environ[count] = entry;
            return 0;
        }
        count++;
    }
    if (!owned_environment || count + 2 > environment_capacity) {
        size_t capacity = count + 16;
        char **vector = calloc(capacity, sizeof(char *));
        if (!vector)
            return -1;
        if (environ)
            memcpy(vector, environ, count * sizeof(char *));
        if (owned_environment)
            free(environ);
        environ = vector;
        owned_environment = true;
        environment_capacity = capacity;
    }
    environ[count] = entry;
    environ[count + 1] = NULL;
    return 0;
}
int setenv(const char *name, const char *value, int overwrite) {
    if (!*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    if (!overwrite && getenv(name))
        return 0;
    size_t n = strlen(name), v = strlen(value);
    if (n > SIZE_MAX - v - 2) {
        errno = ENOMEM;
        return -1;
    }
    char *entry = malloc(n + v + 2);
    struct env_owned *record = malloc(sizeof(*record));
    if (!entry || !record) {
        free(entry);
        free(record);
        return -1;
    }
    memcpy(entry, name, n);
    entry[n] = '=';
    memcpy(entry + n + 1, value, v + 1);
    if (environment_insert(entry) < 0) {
        free(entry);
        free(record);
        return -1;
    }
    *record = (struct env_owned){entry, owned};
    owned = record;
    return 0;
}
int unsetenv(const char *name) {
    size_t n = strlen(name);
    if (!n || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    for (size_t i = 0; environ && environ[i];) {
        if (!strncmp(environ[i], name, n) && environ[i][n] == '=') {
            release_env(environ[i]);
            size_t j = i;
            do {
                environ[j] = environ[j + 1];
                j++;
            } while (environ[j]);
        } else
            i++;
    }
    return 0;
}
int putenv(char *entry) {
    if (!strchr(entry, '='))
        return unsetenv(entry);
    return environment_insert(entry);
}
int clearenv(void) {
    while (owned) {
        struct env_owned *n = owned;
        owned = n->next;
        free(n->text);
        free(n);
    }
    if (owned_environment)
        free(environ);
    environ = NULL;
    owned_environment = false;
    environment_capacity = 0;
    return 0;
}
static uint64_t random_state = 1;
void srand(unsigned seed) {
    random_state = seed ? seed : 1;
}
int rand(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return (int)(random_state & 0x7fffffff);
}
static void (*exit_functions[128])(void);
static unsigned exit_count;
int atexit(void (*fn)(void)) {
    if (exit_count == 128) {
        errno = ENOMEM;
        return -1;
    }
    exit_functions[exit_count++] = fn;
    return 0;
}
_Noreturn void exit(int status) {
    while (exit_count)
        exit_functions[--exit_count]();
    fflush(NULL);
    _exit(status);
}
_Noreturn void abort(void) {
    signal(SIGABRT, SIG_DFL);
    sigset_t set = 0;
    sigaddset(&set, SIGABRT);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    raise(SIGABRT);
    _exit(128 + SIGABRT);
}
void __assert_fail(const char *expression, const char *file, unsigned line, const char *function) {
    fprintf(stderr, "Assertion failed: %s (%s:%u, %s)\n", expression, file, line, function);
    abort();
}
int system(const char *command) {
    if (!command)
        return access("/bin/sh", X_OK) == 0;
    pid_t p = fork();
    if (p < 0)
        return -1;
    if (!p) {
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    int status;
    while (waitpid(p, &status, 0) < 0)
        if (errno != EINTR)
            return -1;
    return status;
}
int pause(void) {
    for (;;) {
        struct timespec t = {3600, 0};
        if (nanosleep(&t, NULL) < 0)
            return -1;
    }
}
extern int main(int, char **, char **);
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);
static void finalize(void) {
    for (void (**p)(void) = __fini_array_end; p > __fini_array_start;)
        (*--p)();
}
_Noreturn void __libc_start(uint64_t *stack) {
    int argc = stack[0];
    char **argv = (void *)(stack + 1);
    environ = argv + argc + 1;
    atexit(finalize);
    for (void (**p)(void) = __init_array_start; p < __init_array_end; p++)
        (*p)();
    exit(main(argc, argv, environ));
}
