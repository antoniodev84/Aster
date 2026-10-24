#ifndef KPRINTF_H
#define KPRINTF_H

#include <stdarg.h>

int kvprintf(const char *format, va_list args) __attribute__((format(printf, 1, 0)));
int kprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));

#endif
