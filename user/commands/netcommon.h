#ifndef ASTER_COMMAND_NETCOMMON_H
#define ASTER_COMMAND_NETCOMMON_H
#include "common.h"
#include <net.h>
static inline int net_number(const char *text, unsigned minimum, unsigned maximum,
                             unsigned *result) {
    if (!text || !*text)
        return -1;
    unsigned value = 0;
    for (; *text; text++) {
        if (*text < '0' || *text > '9')
            return -1;
        unsigned digit = *text - '0';
        if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10))
            return -1;
        value = value * 10 + digit;
    }
    if (value < minimum)
        return -1;
    *result = value;
    return 0;
}
static inline uint64_t net_milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static inline int net_timeout(int fd, unsigned milliseconds) {
    struct timeval timeout = {milliseconds / 1000, (milliseconds % 1000) * 1000};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
        return -1;
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}
static inline int net_send_all(int fd, const void *buffer, size_t length) {
    const uint8_t *p = buffer;
    while (length) {
        ssize_t n = send(fd, p, length, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        p += n;
        length -= n;
    }
    return 0;
}
static inline int net_interface(const char *name, struct abi_netconfig *config) {
    for (unsigned index = 0; index < 4; index++) {
        memset(config, 0, sizeof(*config));
        config->index = index;
        if (netconfig(config, 0) < 0)
            return -1;
        if (!name || !strcmp(name, config->name))
            return 0;
    }
    errno = ENODEV;
    return -1;
}
static inline void net_print_config(const struct abi_netconfig *c) {
    char ip[16], mask[16], gateway[16], dns[16];
    inet_ntop(AF_INET, &c->address, ip, sizeof(ip));
    inet_ntop(AF_INET, &c->netmask, mask, sizeof(mask));
    inet_ntop(AF_INET, &c->gateway, gateway, sizeof(gateway));
    inet_ntop(AF_INET, &c->dns, dns, sizeof(dns));
    printf("%s: %s %s mtu %u%s%s\n", c->name, c->flags & NETCFG_UP ? "UP" : "DOWN",
           c->flags & NETCFG_LINK ? "LINK" : "NO-CARRIER", c->mtu,
           c->flags & NETCFG_DHCP ? " DHCP" : "", c->flags & NETCFG_DEFAULT ? " DEFAULT" : "");
    printf("  inet %s netmask %s gateway %s dns %s\n", ip, mask, gateway, dns);
    printf("  ether %02x:%02x:%02x:%02x:%02x:%02x\n", c->mac[0], c->mac[1], c->mac[2], c->mac[3],
           c->mac[4], c->mac[5]);
}
#endif
