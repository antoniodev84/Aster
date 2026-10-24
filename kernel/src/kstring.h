#ifndef KSTRING_H
#define KSTRING_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "memory.h"
static inline size_t kstrlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
static inline int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
static inline int kstrncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}
static inline size_t kstrlcpy(char *d, const char *s, size_t n) {
    size_t l = kstrlen(s);
    if (n) {
        size_t c = l < n - 1 ? l : n - 1;
        memcpy(d, s, c);
        d[c] = 0;
    }
    return l;
}
static inline char *kstrchr(const char *s, int c) {
    do {
        if (*s == (char)c)
            return (char *)s;
    } while (*s++);
    return NULL;
}
static inline void kappend(char *d, size_t cap, const char *s) {
    size_t n = kstrlen(d);
    if (n < cap)
        kstrlcpy(d + n, s, cap - n);
}
static inline void knumber(char *d, size_t cap, uint64_t v, unsigned base) {
    char b[65];
    size_t n = 0;
    do {
        b[n++] = "0123456789abcdef"[v % base];
        v /= base;
    } while (v);
    while (n) {
        char s[2] = {b[--n], 0};
        kappend(d, cap, s);
    }
}
#endif
