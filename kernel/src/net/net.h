#ifndef ASTER_NETWORK_H
#define ASTER_NETWORK_H
#include <abi.h>
#include <stdbool.h>
struct net_socket;
void net_init(void);
void net_poll(void);
long net_syscall(uint64_t number, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                 uint64_t f);
void net_close(struct net_socket *s);
long net_read(struct net_socket *s, void *buffer, size_t size);
long net_write(struct net_socket *s, const void *buffer, size_t size);
#endif
