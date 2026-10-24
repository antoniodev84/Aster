#include <os.h>
void *memcpy(void *restrict d, const void *restrict s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    for (size_t i = 0; i < n; i++)
        a[i] = b[i];
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    if ((uintptr_t)a < (uintptr_t)b)
        for (size_t i = 0; i < n; i++)
            a[i] = b[i];
    else
        for (size_t i = n; i; i--)
            a[i - 1] = b[i - 1];
    return d;
}
void *memset(void *d, int c, size_t n) {
    unsigned char *a = d;
    for (size_t i = 0; i < n; i++)
        a[i] = c;
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}
void *memchr(const void *a, int c, size_t n) {
    const unsigned char *p = a;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (unsigned char)c)
            return (void *)(p + i);
    return NULL;
}
size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
size_t strnlen(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && s[i])
        i++;
    return i;
}
char *strcpy(char *restrict d, const char *restrict s) {
    size_t n = strlen(s);
    memcpy(d, s, n + 1);
    return d;
}
char *stpcpy(char *restrict d, const char *restrict s) {
    size_t n = strlen(s);
    memcpy(d, s, n + 1);
    return d + n;
}
char *strncpy(char *restrict d, const char *restrict s, size_t n) {
    size_t i = 0;
    while (i < n && s[i]) {
        d[i] = s[i];
        i++;
    }
    while (i < n)
        d[i++] = 0;
    return d;
}
char *strcat(char *restrict d, const char *restrict s) {
    strcpy(d + strlen(d), s);
    return d;
}
char *strncat(char *restrict d, const char *restrict s, size_t n) {
    size_t a = strlen(d), b = strnlen(s, n);
    memcpy(d + a, s, b);
    d[a + b] = 0;
    return d;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int d = (unsigned char)a[i] - (unsigned char)b[i];
        if (d || !a[i])
            return d;
    }
    return 0;
}
int strcasecmp(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
int strncasecmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int d = tolower((unsigned char)a[i]) - tolower((unsigned char)b[i]);
        if (d || !a[i])
            return d;
    }
    return 0;
}
char *strchr(const char *s, int c) {
    do {
        if (*s == (char)c)
            return (char *)s;
    } while (*s++);
    return NULL;
}
char *strrchr(const char *s, int c) {
    const char *p = NULL;
    do {
        if (*s == (char)c)
            p = s;
    } while (*s++);
    return (char *)p;
}
char *strstr(const char *s, const char *n) {
    if (!*n)
        return (char *)s;
    size_t len = strlen(n);
    for (; *s; s++)
        if (!strncmp(s, n, len))
            return (char *)s;
    return NULL;
}
size_t strspn(const char *s, const char *a) {
    size_t n = 0;
    while (s[n] && strchr(a, s[n]))
        n++;
    return n;
}
size_t strcspn(const char *s, const char *r) {
    size_t n = 0;
    while (s[n] && !strchr(r, s[n]))
        n++;
    return n;
}
char *strpbrk(const char *s, const char *a) {
    while (*s) {
        if (strchr(a, *s))
            return (char *)s;
        s++;
    }
    return NULL;
}
char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}
char *strndup(const char *s, size_t n) {
    n = strnlen(s, n);
    char *d = malloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}
char *strtok_r(char *restrict s, const char *restrict d, char **restrict state) {
    if (!s)
        s = *state;
    if (!s)
        return NULL;
    s += strspn(s, d);
    if (!*s) {
        *state = NULL;
        return NULL;
    }
    char *end = s + strcspn(s, d);
    if (*end) {
        *end = 0;
        *state = end + 1;
    } else
        *state = NULL;
    return s;
}
char *strtok(char *restrict s, const char *restrict d) {
    static char *state;
    return strtok_r(s, d, &state);
}
char *strsep(char **s, const char *d) {
    if (!*s)
        return NULL;
    char *start = *s, *end = start + strcspn(start, d);
    if (*end) {
        *end = 0;
        *s = end + 1;
    } else
        *s = NULL;
    return start;
}
int isalpha(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
int isdigit(int c) {
    return c >= '0' && c <= '9';
}
int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}
int isspace(int c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}
int isupper(int c) {
    return c >= 'A' && c <= 'Z';
}
int islower(int c) {
    return c >= 'a' && c <= 'z';
}
int isxdigit(int c) {
    return isdigit(c) || (tolower(c) >= 'a' && tolower(c) <= 'f');
}
int isprint(int c) {
    return c >= 32 && c < 127;
}
int isgraph(int c) {
    return c > 32 && c < 127;
}
int ispunct(int c) {
    return isgraph(c) && !isalnum(c);
}
int iscntrl(int c) {
    return (c >= 0 && c < 32) || c == 127;
}
int isblank(int c) {
    return c == ' ' || c == '\t';
}
int tolower(int c) {
    return isupper(c) ? c + 32 : c;
}
int toupper(int c) {
    return islower(c) ? c - 32 : c;
}
char *strerror(int e) {
    switch (e) {
    case 0:
        return "Success";
    case EPERM:
        return "Operation not permitted";
    case ENOENT:
        return "No such file or directory";
    case ESRCH:
        return "No such process";
    case EINTR:
        return "Interrupted system call";
    case EIO:
        return "Input/output error";
    case ENXIO:
        return "No such device or address";
    case E2BIG:
        return "Argument list too long";
    case ENOEXEC:
        return "Exec format error";
    case EBADF:
        return "Bad file descriptor";
    case ECHILD:
        return "No child processes";
    case EAGAIN:
        return "Resource temporarily unavailable";
    case ENOMEM:
        return "Cannot allocate memory";
    case EACCES:
        return "Permission denied";
    case EFAULT:
        return "Bad address";
    case EBUSY:
        return "Resource busy";
    case EEXIST:
        return "File exists";
    case EXDEV:
        return "Cross-device link";
    case ENODEV:
        return "No such device";
    case ENOTDIR:
        return "Not a directory";
    case EISDIR:
        return "Is a directory";
    case EINVAL:
        return "Invalid argument";
    case ENFILE:
        return "File table overflow";
    case EMFILE:
        return "Too many open files";
    case ENOTTY:
        return "Inappropriate ioctl for device";
    case EFBIG:
        return "File too large";
    case ENOSPC:
        return "No space left on device";
    case ESPIPE:
        return "Illegal seek";
    case EROFS:
        return "Read-only filesystem";
    case EMLINK:
        return "Too many links";
    case EPIPE:
        return "Broken pipe";
    case ERANGE:
        return "Result out of range";
    case ENAMETOOLONG:
        return "File name too long";
    case ENOSYS:
        return "Function not implemented";
    case ENOTEMPTY:
        return "Directory not empty";
    case ELOOP:
        return "Too many symbolic links";
    case EOVERFLOW:
        return "Value too large";
    case ENOTSOCK: return "Socket operation on non-socket";
    case EDESTADDRREQ: return "Destination address required";
    case EMSGSIZE: return "Message too long";
    case EPROTOTYPE: return "Wrong socket protocol type";
    case ENOPROTOOPT: return "Protocol option unavailable";
    case EPROTONOSUPPORT: return "Protocol not supported";
    case EAFNOSUPPORT: return "Address family not supported";
    case EADDRINUSE: return "Address already in use";
    case EADDRNOTAVAIL: return "Address not available";
    case ENETDOWN: return "Network is down";
    case ENETUNREACH: return "Network is unreachable";
    case ECONNABORTED: return "Connection aborted";
    case ECONNRESET: return "Connection reset by peer";
    case ENOBUFS: return "No buffer space available";
    case EISCONN: return "Socket is already connected";
    case ENOTCONN: return "Socket is not connected";
    case ETIMEDOUT: return "Connection timed out";
    case ECONNREFUSED: return "Connection refused";
    case EHOSTUNREACH: return "Host is unreachable";
    case EALREADY: return "Operation already in progress";
    case EINPROGRESS: return "Operation in progress";
    case ENOTSUP:
        return "Operation not supported";
    default:
        return "Unknown error";
    }
}
void perror(const char *s) {
    int e = errno;
    if (s && *s)
        fprintf(stderr, "%s: %s\n", s, strerror(e));
    else
        fprintf(stderr, "%s\n", strerror(e));
}
