#ifndef ASTER_ARPA_INET_H
#define ASTER_ARPA_INET_H
#include <netinet/in.h>
int inet_pton(int family, const char *text, void *address);
const char *inet_ntop(int family, const void *address, char *text, socklen_t size);
in_addr_t inet_addr(const char *text);
char *inet_ntoa(struct in_addr address);
#endif
