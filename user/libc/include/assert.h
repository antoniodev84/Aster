#ifndef ASSERT_H
#define ASSERT_H
#include <os.h>
void __assert_fail(const char *, const char *, unsigned, const char *);
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression)                                                                         \
    ((expression) ? (void)0 : __assert_fail(#expression, __FILE__, __LINE__, __func__))
#endif
#endif
