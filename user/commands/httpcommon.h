#ifndef ASTER_HTTP_COMMON_H
#define ASTER_HTTP_COMMON_H
#include "netcommon.h"
struct http_reader {
    int fd;
    uint8_t buffer[4096];
    size_t offset, used;
};
static inline ssize_t http_read(struct http_reader *r, void *buffer, size_t length) {
    if (!length)
        return 0;
    if (r->offset == r->used) {
        ssize_t n;
        do {
            n = recv(r->fd, r->buffer, sizeof(r->buffer), 0);
        } while (n < 0 && errno == EINTR);
        if (n <= 0)
            return n;
        r->offset = 0;
        r->used = n;
    }
    size_t count = r->used - r->offset;
    if (count > length)
        count = length;
    memcpy(buffer, r->buffer + r->offset, count);
    r->offset += count;
    return count;
}
static inline int http_exact(struct http_reader *r, void *buffer, size_t length) {
    uint8_t *p = buffer;
    while (length) {
        ssize_t n = http_read(r, p, length);
        if (n <= 0) {
            if (!n)
                errno = EIO;
            return -1;
        }
        p += n;
        length -= n;
    }
    return 0;
}
static inline int http_line(struct http_reader *r, char *line, size_t size) {
    size_t used = 0;
    for (;;) {
        char c;
        if (http_exact(r, &c, 1) < 0)
            return -1;
        if (c == '\n') {
            if (!used || line[used - 1] != '\r') {
                errno = EIO;
                return -1;
            }
            line[--used] = 0;
            return used;
        }
        if (!c || ((unsigned char)c < 32 && c != '\r' && c != '\t')) {
            errno = EIO;
            return -1;
        }
        if (used + 1 >= size) {
            errno = EOVERFLOW;
            return -1;
        }
        line[used++] = c;
    }
}
static inline int http_equal(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y)
            return 0;
    }
    return !*a && !*b;
}
static inline char *http_header(char *line) {
    char *colon = strchr(line, ':');
    if (!colon || colon == line)
        return NULL;
    *colon++ = 0;
    while (*colon == ' ' || *colon == '\t')
        colon++;
    char *end = colon + strlen(colon);
    while (end > colon && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = 0;
    return colon;
}
static inline int http_decimal(const char *s, uint64_t *value) {
    if (!*s)
        return -1;
    uint64_t n = 0;
    while (*s) {
        if (*s < '0' || *s > '9' || n > (UINT64_MAX - (unsigned)(*s - '0')) / 10)
            return -1;
        n = n * 10 + (unsigned)(*s++ - '0');
    }
    *value = n;
    return 0;
}
#endif
