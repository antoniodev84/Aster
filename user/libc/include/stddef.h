#ifndef STDDEF_H
#define STDDEF_H
#define NULL ((void *)0)
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __WCHAR_TYPE__ wchar_t;
typedef struct {
    long long integer;
    long double floating;
} max_align_t;
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
