#ifndef ASTER_NET_DRIVER_H
#define ASTER_NET_DRIVER_H
#include <abi.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct net_interface {
    uint32_t address, netmask, gateway;
    uint8_t mac[6];
    bool up, link;
};
extern struct net_interface ethernet_interfaces[4];
extern unsigned ethernet_count;
void ethernet_init(void);
void ethernet_poll(void);
long ethernet_send(unsigned index, const void *frame, size_t length);
void net_ethernet_input(unsigned index, const void *frame, size_t length);
#endif
