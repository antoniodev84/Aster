#ifndef ASTER_USER_NET_H
#define ASTER_USER_NET_H
#include <arpa/inet.h>
#include <netdb.h>
int netconfig(struct abi_netconfig *config, int set);
int netresolve(const char *name, struct in_addr *address);
int netdhcp(unsigned index, unsigned action);
#endif
