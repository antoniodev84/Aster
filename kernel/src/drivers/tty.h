#ifndef TTY_H
#define TTY_H
#include <stddef.h>
#include <stdint.h>
void tty_init(void);
void tty_poll(void);
long tty_read(void *buffer, size_t count);
long tty_write(const void *buffer, size_t count);
long tty_ioctl(unsigned long request, void *argument);
#endif
