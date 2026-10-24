#ifndef ASTER_NETWORK_CORE_H
#define ASTER_NETWORK_CORE_H
#include "net.h"
#include "drivers/net.h"
#include "mm/mm.h"
#include "kstring.h"
#include "proc/process.h"
#define NET_WINDOW 32768u
struct tcp_connection;
struct net_datagram {
    struct net_datagram *next;
    struct abi_sockaddr_in peer;
    size_t length;
    uint8_t data[];
};
struct net_socket {
    struct net_socket *next;
    int type, error;
    bool bound, connecting, connected, listening, eof, read_shutdown, write_shutdown;
    bool reuse, broadcast;
    unsigned backlog, queued, accepted_count;
    uint64_t receive_timeout, send_timeout;
    struct abi_sockaddr_in local, peer;
    struct net_datagram *head, *tail;
    struct tcp_connection *tcp;
    struct net_socket *accepted[16];
};
extern struct net_socket *net_sockets;
extern unsigned net_default_interface;
extern uint32_t net_dns_server;
static inline uint16_t net_get16(const void *pointer) {
    const uint8_t *p = pointer;
    return (uint16_t)p[0] << 8 | p[1];
}
static inline uint32_t net_get32(const void *pointer) {
    const uint8_t *p = pointer;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline void net_put16(void *pointer, uint16_t v) {
    uint8_t *p = pointer;
    p[0] = v >> 8;
    p[1] = v;
}
static inline void net_put32(void *pointer, uint32_t v) {
    uint8_t *p = pointer;
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}
static inline uint32_t net_ip(const void *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
static inline uint32_t net_ipv4(unsigned a, unsigned b, unsigned c, unsigned d) {
    uint8_t p[4] = {a, b, c, d};
    return net_ip(p);
}
static inline uint16_t net_swap16(uint16_t v) {
    return __builtin_bswap16(v);
}
uint32_t net_random(void);
uint16_t net_checksum(const void *data, size_t length, uint32_t sum);
uint32_t net_pseudo(uint32_t source, uint32_t destination, uint8_t protocol, size_t length);
int net_route(uint32_t source, uint32_t destination);
uint32_t net_source(int interface);
long net_ip_send(int interface, uint32_t source, uint32_t destination, uint8_t protocol,
                 const void *payload, size_t length);
long net_udp_send(int interface, uint32_t source, uint32_t destination, uint16_t source_port,
                  uint16_t destination_port, const void *payload, size_t length);
void net_wire_poll(void);
void net_arp_probe(unsigned interface, uint32_t address, bool announce);
void net_dhcp_conflict(unsigned interface, uint32_t source, uint32_t target);
bool net_services_port_used(uint16_t port);
struct net_socket *net_socket_allocate(int type);
void net_deliver(struct net_socket *s, uint32_t source, uint16_t port, const void *data,
                 size_t length);
void net_wake(void);
uint16_t net_ephemeral(int type, uint32_t address);
struct net_socket *net_passive_socket(struct net_socket *listener,
                                      struct tcp_connection *connection, uint32_t local,
                                      uint16_t local_port, uint32_t peer, uint16_t peer_port);
long net_tcp_connect(struct net_socket *s);
long net_tcp_send(struct tcp_connection *c, const void *data, size_t length);
long net_tcp_receive(struct tcp_connection *c, void *data, size_t length, bool peek);
void net_tcp_close(struct tcp_connection *c, bool discard);
void net_tcp_shutdown(struct tcp_connection *c, bool receive, bool send);
void net_tcp_detach_listener(struct net_socket *s);
bool net_tcp_port_used(uint32_t address, uint16_t port, bool reuse);
void net_tcp_input(uint32_t source, uint32_t destination, const uint8_t *data, size_t length);
void net_tcp_poll(void);
bool net_services_udp(unsigned interface, uint32_t source, uint16_t sport, uint16_t dport,
                      const uint8_t *data, size_t length);
void net_services_poll(void);
long net_dns_lookup(const char *name, uint32_t *address);
long net_dhcp(unsigned interface, unsigned action);
bool net_dhcp_bound(unsigned interface);
void net_dhcp_stop(unsigned interface);
#endif
