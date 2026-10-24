#include "core.h"

uint32_t net_dns_server;
static uint32_t dns_secondary;
struct dns_query {
    char name[256], question[256];
    int state;
    uint16_t id, port;
    unsigned attempts, cname_depth;
    uint32_t server, address, ttl;
    uint64_t next_send, expires;
    struct net_socket *tcp;
    uint8_t request[516], prefix[2];
    uint8_t *response;
    size_t request_length, sent, prefix_used, response_length, received;
};
static struct dns_query dns_queries[16];
struct lease {
    unsigned state, attempts, probes;
    bool conflict;
    uint32_t xid, server, candidate, mask, gateway, dns, dns2, seconds, t1, t2;
    uint64_t next_send, expires, renew, rebind, acquired;
};
static struct lease leases[4];
bool net_services_port_used(uint16_t port) {
    if (port == 68)
        return true;
    for (unsigned i = 0; i < 16; i++)
        if (dns_queries[i].state == 1 && dns_queries[i].port == port)
            return true;
    return false;
}
static bool name_equal(const char *a, const char *b) {
    for (;;) {
        unsigned char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return false;
        if (!x)
            return true;
    }
}
static bool numeric(const char *text, uint32_t *address) {
    uint8_t bytes[4];
    for (unsigned i = 0; i < 4; i++) {
        if (*text < '0' || *text > '9')
            return false;
        unsigned value = 0, digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (*text++ - '0');
            if (++digits > 3 || value > 255)
                return false;
        }
        bytes[i] = value;
        if (i < 3 && *text++ != '.')
            return false;
    }
    if (*text)
        return false;
    *address = net_ip(bytes);
    return true;
}
static int dns_encode(uint8_t *p, size_t capacity, const char *name) {
    size_t used = 0;
    while (*name) {
        const char *end = name;
        while (*end && *end != '.')
            end++;
        size_t length = end - name;
        if (!length || length > 63 || used + length + 2 > capacity)
            return -EINVAL;
        p[used++] = length;
        memcpy(p + used, name, length);
        used += length;
        name = *end ? end + 1 : end;
    }
    if (used > 254)
        return -EINVAL;
    p[used++] = 0;
    return used;
}
static bool dns_name(const uint8_t *p, size_t length, size_t *position, char output[256]) {
    size_t cursor = *position, end = 0, used = 0;
    unsigned hops = 0;
    for (;;) {
        if (cursor >= length || ++hops > 128)
            return false;
        unsigned size = p[cursor++];
        if ((size & 0xc0) == 0xc0) {
            if (cursor >= length)
                return false;
            size_t target = ((size & 63) << 8) | p[cursor++];
            if (!end)
                end = cursor;
            if (target >= length || target >= cursor - 2)
                return false;
            cursor = target;
            continue;
        }
        if (size & 0xc0 || size > 63 || cursor + size > length)
            return false;
        if (!size)
            break;
        if (used && used + 1 < 256)
            output[used++] = '.';
        if (used + size >= 256)
            return false;
        for (unsigned i = 0; i < size; i++) {
            uint8_t c = p[cursor + i];
            if (!c || c == '.')
                return false;
            output[used++] = c;
        }
        cursor += size;
    }
    output[used] = 0;
    *position = end ? end : cursor;
    return true;
}
static void dns_finish(struct dns_query *q, bool success, uint32_t address, uint32_t ttl) {
    if (q->tcp) {
        net_close(q->tcp);
        q->tcp = NULL;
    }
    kfree(q->response);
    q->response = NULL;
    q->state = success ? 2 : -1;
    q->address = address;
    q->ttl = ttl;
    uint64_t cache = success ? (uint64_t)ttl * 1000 : 1000;
    if (cache < 1000)
        cache = 1000;
    if (cache > 3600000)
        cache = 3600000;
    q->expires = hal_time_ms() + cache;
    net_wake();
}
static int dns_request(struct dns_query *q) {
    uint8_t *p = q->request + 2;
    memset(q->request, 0, sizeof(q->request));
    net_put16(p, q->id);
    net_put16(p + 2, 0x100);
    net_put16(p + 4, 1);
    int length = dns_encode(p + 12, sizeof(q->request) - 18, q->question);
    if (length < 0)
        return length;
    net_put16(p + 12 + length, 1);
    net_put16(p + 14 + length, 1);
    q->request_length = 18 + length;
    net_put16(q->request, q->request_length - 2);
    return 0;
}
static void dns_tcp_start(struct dns_query *q) {
    if (q->tcp)
        return;
    int route = net_route(0, q->server);
    if (route < 0) {
        dns_finish(q, false, 0, 0);
        return;
    }
    struct net_socket *s = net_socket_allocate(SOCK_STREAM);
    if (!s) {
        dns_finish(q, false, 0, 0);
        return;
    }
    uint16_t port = net_ephemeral(SOCK_STREAM, net_source(route));
    if (!port) {
        net_close(s);
        dns_finish(q, false, 0, 0);
        return;
    }
    s->local = (struct abi_sockaddr_in){AF_INET, net_swap16(port), net_source(route), {0}};
    s->peer = (struct abi_sockaddr_in){AF_INET, net_swap16(53), q->server, {0}};
    s->bound = true;
    if (net_tcp_connect(s) < 0) {
        net_close(s);
        dns_finish(q, false, 0, 0);
        return;
    }
    q->tcp = s;
    q->sent = q->prefix_used = q->received = 0;
    q->next_send = hal_time_ms() + 15000;
}
static void dns_input(struct dns_query *q, const uint8_t *p, size_t length, bool tcp) {
    if (length < 12 || net_get16(p) != q->id || !(p[2] & 0x80) || (p[2] & 0x78) ||
        net_get16(p + 4) != 1)
        return;
    size_t position = 12;
    char question[256];
    if (!dns_name(p, length, &position, question) || position + 4 > length ||
        !name_equal(question, q->question) || net_get16(p + position) != 1 ||
        net_get16(p + position + 2) != 1)
        return;
    position += 4;
    if (p[2] & 2) {
        if (tcp)
            dns_finish(q, false, 0, 0);
        else
            dns_tcp_start(q);
        return;
    }
    if (p[3] & 15) {
        dns_finish(q, false, 0, 0);
        return;
    }
    unsigned answers = net_get16(p + 6);
    if (answers > 1024)
        return;
    char desired[256];
    kstrlcpy(desired, q->question, sizeof(desired));
    uint32_t minimum_ttl = q->ttl;
    for (unsigned chain = 0; chain < 9; chain++) {
        size_t at = position;
        bool changed = false;
        for (unsigned i = 0; i < answers; i++) {
            char owner[256];
            if (!dns_name(p, length, &at, owner) || at + 10 > length)
                return;
            uint16_t type = net_get16(p + at), class = net_get16(p + at + 2),
                     size = net_get16(p + at + 8);
            uint32_t ttl = net_get32(p + at + 4);
            at += 10;
            if (size > length - at)
                return;
            if (class == 1 && name_equal(owner, desired)) {
                if (ttl < minimum_ttl)
                    minimum_ttl = ttl;
                if (type == 1 && size == 4) {
                    dns_finish(q, true, net_ip(p + at), minimum_ttl);
                    return;
                }
                if (type == 5) {
                    size_t target = at;
                    char alias[256];
                    if (!dns_name(p, length, &target, alias) || target > at + size || !alias[0])
                        return;
                    if (++q->cname_depth > 8) {
                        dns_finish(q, false, 0, 0);
                        return;
                    }
                    kstrlcpy(desired, alias, sizeof(desired));
                    changed = true;
                    break;
                }
            }
            at += size;
        }
        if (!changed) {
            if (!name_equal(desired, q->question)) {
                if (q->tcp) {
                    net_close(q->tcp);
                    q->tcp = NULL;
                }
                kfree(q->response);
                q->response = NULL;
                q->ttl = minimum_ttl;
                kstrlcpy(q->question, desired, sizeof(q->question));
                q->id = net_random();
                q->attempts = 0;
                q->next_send = 0;
                if (dns_request(q) < 0)
                    dns_finish(q, false, 0, 0);
            } else
                dns_finish(q, false, 0, 0);
            return;
        }
    }
    dns_finish(q, false, 0, 0);
}
long net_dns_lookup(const char *name, uint32_t *address) {
    if (numeric(name, address))
        return 0;
    if (name_equal(name, "localhost") || name_equal(name, "localhost.")) {
        *address = net_ipv4(127, 0, 0, 1);
        return 0;
    }
    size_t length = kstrlen(name);
    if (!length || length > 254)
        return -EINVAL;
    char canonical[256];
    kstrlcpy(canonical, name, sizeof(canonical));
    if (canonical[length - 1] == '.')
        canonical[--length] = 0;
    uint64_t now = hal_time_ms();
    struct dns_query *empty = NULL;
    for (unsigned i = 0; i < 16; i++) {
        struct dns_query *q = &dns_queries[i];
        if (q->state && name_equal(q->name, canonical)) {
            if (q->state == 1)
                return -EAGAIN;
            if (now < q->expires) {
                if (q->state < 0)
                    return -EHOSTUNREACH;
                *address = q->address;
                return 0;
            }
        }
        if (!q->state || (q->state != 1 && now >= q->expires))
            empty = q;
    }
    if (!empty)
        return -ENOBUFS;
    if (!net_dns_server)
        return -ENETUNREACH;
    struct dns_query *q = empty;
    memset(q, 0, sizeof(*q));
    kstrlcpy(q->name, canonical, sizeof(q->name));
    kstrlcpy(q->question, canonical, sizeof(q->question));
    q->ttl = UINT32_MAX;
    q->id = net_random();
    q->port = net_ephemeral(SOCK_DGRAM, 0);
    q->server = net_dns_server;
    if (!q->port)
        return -EADDRINUSE;
    if (dns_request(q) < 0)
        return -EINVAL;
    q->state = 1;
    return -EAGAIN;
}
static void dhcp_option(uint8_t *p, size_t *position, uint8_t code, const void *value,
                        size_t length) {
    p[(*position)++] = code;
    p[(*position)++] = length;
    memcpy(p + *position, value, length);
    *position += length;
}
static long dhcp_send(unsigned interface, unsigned type) {
    struct lease *l = &leases[interface];
    struct net_interface *n = &ethernet_interfaces[interface];
    uint8_t p[320] = {0};
    p[0] = 1;
    p[1] = 1;
    p[2] = 6;
    net_put32(p + 4, l->xid);
    bool assigned = l->state == 3 || l->state == 4 || l->state == 5 || type == 7;
    if (assigned)
        memcpy(p + 12, &n->address, 4);
    else
        net_put16(p + 10, 0x8000);
    memcpy(p + 28, n->mac, 6);
    net_put32(p + 236, 0x63825363);
    size_t used = 240;
    uint8_t message = type;
    dhcp_option(p, &used, 53, &message, 1);
    uint8_t client[7] = {1};
    memcpy(client + 1, n->mac, 6);
    dhcp_option(p, &used, 61, client, 7);
    if ((type == 3 && l->state == 2) || type == 4)
        dhcp_option(p, &used, 50, &l->candidate, 4);
    if ((type == 3 && l->state == 2) || type == 4 || type == 7)
        dhcp_option(p, &used, 54, &l->server, 4);
    if (type == 1 || type == 3) {
        uint8_t wanted[] = {1, 3, 6, 51, 58, 59};
        dhcp_option(p, &used, 55, wanted, sizeof(wanted));
        uint8_t maximum[2];
        net_put16(maximum, 1500);
        dhcp_option(p, &used, 57, maximum, 2);
        dhcp_option(p, &used, 12, "aster", 5);
    }
    p[used++] = 255;
    if (used < 300)
        used = 300;
    uint32_t destination = (type == 7 || (type == 3 && l->state == 4)) ? l->server : UINT32_MAX;
    return net_udp_send(interface, assigned ? n->address : 0, destination, 68, 67, p, used);
}
struct dhcp_options {
    uint32_t mask, gateway, dns, dns2, server, lease, t1, t2;
    uint8_t type, overload;
    bool has_mask, has_router, has_dns, has_lease;
};
static bool dhcp_options_read(const uint8_t *p, size_t length, struct dhcp_options *o,
                              bool overload) {
    size_t at = 0;
    while (at < length) {
        unsigned type = p[at++];
        if (!type)
            continue;
        if (type == 255)
            return true;
        if (at >= length)
            return false;
        unsigned count = p[at++];
        if (count > length - at)
            return false;
        const uint8_t *value = p + at;
        switch (type) {
        case 1:
            if (count != 4)
                return false;
            o->mask = net_ip(value);
            o->has_mask = true;
            break;
        case 3:
            if (!count || count % 4)
                return false;
            o->gateway = net_ip(value);
            o->has_router = true;
            break;
        case 6:
            if (!count || count % 4)
                return false;
            o->dns = net_ip(value);
            if (count >= 8)
                o->dns2 = net_ip(value + 4);
            o->has_dns = true;
            break;
        case 51:
            if (count != 4)
                return false;
            o->lease = net_get32(value);
            o->has_lease = true;
            break;
        case 52:
            if (!overload || count != 1 || value[0] > 3)
                return false;
            o->overload = value[0];
            break;
        case 53:
            if (count != 1)
                return false;
            o->type = value[0];
            break;
        case 54:
            if (count != 4)
                return false;
            o->server = net_ip(value);
            break;
        case 58:
            if (count != 4)
                return false;
            o->t1 = net_get32(value);
            break;
        case 59:
            if (count != 4)
                return false;
            o->t2 = net_get32(value);
            break;
        default:
            break;
        }
        at += count;
    }
    return true;
}
static void lease_apply(unsigned interface) {
    struct lease *l = &leases[interface];
    struct net_interface *n = &ethernet_interfaces[interface];
    uint64_t now = l->acquired, duration = (uint64_t)l->seconds * 1000;
    if (l->seconds != UINT32_MAX && hal_time_ms() >= now + duration) {
        l->state = 1;
        l->xid = net_random();
        l->attempts = 0;
        l->next_send = 0;
        return;
    }
    n->address = l->candidate;
    n->netmask = l->mask;
    n->gateway = l->gateway;
    if (interface == net_default_interface || !net_dns_server) {
        net_dns_server = l->dns;
        dns_secondary = l->dns2;
    }
    if (l->seconds == UINT32_MAX)
        l->expires = l->renew = l->rebind = UINT64_MAX;
    else {
        uint64_t t1 = (uint64_t)l->t1 * 1000, t2 = (uint64_t)l->t2 * 1000;
        if (!t1 || t1 >= duration)
            t1 = duration / 2;
        if (t2 <= t1 || t2 >= duration)
            t2 = duration * 7 / 8;
        l->expires = now + duration;
        l->renew = now + t1;
        l->rebind = now + t2;
    }
    l->state = 3;
    l->attempts = 0;
    net_arp_probe(interface, l->candidate, true);
    net_wake();
}
static void lease_clear(unsigned interface) {
    struct net_interface *n = &ethernet_interfaces[interface];
    n->address = n->netmask = n->gateway = 0;
    if (net_default_interface == interface)
        net_dns_server = dns_secondary = 0;
}
static void dhcp_receive(unsigned interface, uint32_t source, const uint8_t *p, size_t length) {
    if (interface >= ethernet_count || length < 240 || p[0] != 2 || p[1] != 1 || p[2] != 6 ||
        net_get32(p + 236) != 0x63825363)
        return;
    struct lease *l = &leases[interface];
    struct net_interface *n = &ethernet_interfaces[interface];
    if (!l->state || net_get32(p + 4) != l->xid || memcmp(p + 28, n->mac, 6))
        return;
    struct dhcp_options o = {0};
    if (!dhcp_options_read(p + 240, length - 240, &o, true) ||
        ((o.overload & 1) && !dhcp_options_read(p + 108, 128, &o, false)) ||
        ((o.overload & 2) && !dhcp_options_read(p + 44, 64, &o, false)))
        return;
    uint32_t offered = net_ip(p + 16);
    bool usable = offered && ((uint8_t *)&offered)[0] > 0 && ((uint8_t *)&offered)[0] < 224 &&
                  ((uint8_t *)&offered)[0] != 127 && offered != UINT32_MAX;
    if (o.type == 2 && l->state == 1 && usable && o.server && ((uint8_t *)&o.server)[0] > 0 &&
        ((uint8_t *)&o.server)[0] < 224 && ((uint8_t *)&o.server)[0] != 127) {
        l->candidate = offered;
        l->server = o.server;
        if (o.has_mask)
            l->mask = o.mask;
        if (o.has_router)
            l->gateway = o.gateway;
        if (o.has_dns) {
            l->dns = o.dns;
            l->dns2 = o.dns2;
        }
        l->state = 2;
        l->attempts = 0;
        l->next_send = 0;
        return;
    }
    if (l->state != 2 && l->state != 4 && l->state != 5)
        return;
    if ((l->state == 2 || l->state == 4) && o.server && o.server != l->server)
        return;
    if (!net_ip(p + 24) && o.server && source != o.server)
        return;
    if (o.type == 6) {
        lease_clear(interface);
        l->state = 1;
        l->xid = net_random();
        l->attempts = 0;
        l->next_send = hal_time_ms() + 1000;
        return;
    }
    if (o.type != 5 || !o.has_lease || !o.lease)
        return;
    if (offered) {
        if (!usable)
            return;
        l->candidate = offered;
    }
    if (!l->candidate)
        return;
    if (o.server)
        l->server = o.server;
    if (o.has_mask)
        l->mask = o.mask;
    uint32_t inverse = ~__builtin_bswap32(l->mask);
    if (!l->mask || (inverse & (inverse + 1)))
        return;
    if (o.has_router)
        l->gateway = o.gateway;
    if (o.has_dns) {
        l->dns = o.dns;
        l->dns2 = o.dns2;
    }
    l->acquired = hal_time_ms();
    l->seconds = o.lease;
    l->t1 = o.t1;
    l->t2 = o.t2;
    if (n->address == l->candidate)
        lease_apply(interface);
    else {
        l->state = 6;
        l->probes = 0;
        l->conflict = false;
        l->next_send = hal_time_ms() + (net_random() % 250);
    }
}
void net_dhcp_conflict(unsigned interface, uint32_t source, uint32_t target) {
    struct lease *l = &leases[interface];
    if (l->state == 6 && (source == l->candidate || (!source && target == l->candidate)))
        l->conflict = true;
}
bool net_dhcp_bound(unsigned interface) {
    return interface < ethernet_count && leases[interface].state >= 3 &&
           leases[interface].state <= 5;
}
void net_dhcp_stop(unsigned interface) {
    if (interface < ethernet_count)
        memset(&leases[interface], 0, sizeof(leases[interface]));
}
long net_dhcp(unsigned interface, unsigned action) {
    if (interface >= ethernet_count)
        return -ENODEV;
    struct lease *l = &leases[interface];
    if (action == 1)
        return net_dhcp_bound(interface);
    if (action == 3) {
        net_dhcp_stop(interface);
        return 0;
    }
    if (action == 2) {
        long result = 0;
        if (net_dhcp_bound(interface))
            result = dhcp_send(interface, 7);
        lease_clear(interface);
        net_dhcp_stop(interface);
        return result;
    }
    if (action != 0)
        return -EINVAL;
    if (net_dhcp_bound(interface))
        return 0;
    lease_clear(interface);
    memset(l, 0, sizeof(*l));
    l->state = 1;
    l->xid = net_random();
    ethernet_interfaces[interface].up = true;
    return 0;
}
bool net_services_udp(unsigned interface, uint32_t source, uint16_t sport, uint16_t dport,
                      const uint8_t *p, size_t length) {
    if (sport == 67 && dport == 68 && interface < ethernet_count) {
        dhcp_receive(interface, source, p, length);
        return true;
    }
    if (sport != 53)
        return false;
    for (unsigned i = 0; i < 16; i++) {
        struct dns_query *q = &dns_queries[i];
        if (q->state == 1 && q->port == dport && q->server == source && !q->tcp) {
            dns_input(q, p, length, false);
            return true;
        }
    }
    return false;
}
void net_services_poll(void) {
    uint64_t now = hal_time_ms();
    for (unsigned i = 0; i < ethernet_count; i++) {
        struct lease *l = &leases[i];
        struct net_interface *n = &ethernet_interfaces[i];
        if (!l->state || !n->up || !n->link)
            continue;
        if (l->state >= 3 && l->state <= 5 && now >= l->expires) {
            lease_clear(i);
            l->state = 1;
            l->xid = net_random();
            l->attempts = 0;
            l->next_send = 0;
        }
        if (l->state == 3 && now >= l->renew) {
            l->state = 4;
            l->xid = net_random();
            l->attempts = 0;
            l->next_send = 0;
        }
        if (l->state == 4 && now >= l->rebind) {
            l->state = 5;
            l->attempts = 0;
            l->next_send = 0;
        }
        if (l->state == 6 && now >= l->next_send) {
            if (l->conflict) {
                dhcp_send(i, 4);
                l->state = 7;
                l->next_send = now + 10000;
            } else if (l->probes < 3) {
                net_arp_probe(i, l->candidate, false);
                l->probes++;
                l->next_send = now + 1000;
            } else
                lease_apply(i);
        }
        if (l->state == 7 && now >= l->next_send) {
            l->state = 1;
            l->xid = net_random();
            l->attempts = 0;
            l->next_send = 0;
        }
        if ((l->state == 1 || l->state == 2 || l->state == 4 || l->state == 5) &&
            now >= l->next_send) {
            if (l->state == 2 && l->attempts >= 4) {
                l->state = 1;
                l->xid = net_random();
                l->attempts = 0;
            }
            long result = dhcp_send(i, l->state == 1 ? 1 : 3);
            if (result == 0)
                l->attempts++;
            unsigned shift = l->attempts > 4 ? 4 : l->attempts;
            uint64_t delay = result < 0 ? 1000 : 1000ull << shift;
            if (l->state == 4 && delay > (l->rebind - now) / 2)
                delay = (l->rebind - now) / 2;
            if (l->state == 5 && delay > (l->expires - now) / 2)
                delay = (l->expires - now) / 2;
            if (delay < 1000)
                delay = 1000;
            l->next_send = now + delay;
        }
    }
    for (unsigned i = 0; i < 16; i++) {
        struct dns_query *q = &dns_queries[i];
        if (q->state != 1)
            continue;
        if (q->tcp) {
            struct net_socket *s = q->tcp;
            if (s->error || now >= q->next_send) {
                dns_finish(q, false, 0, 0);
                continue;
            }
            if (!s->connected || !s->tcp)
                continue;
            if (q->sent < q->request_length) {
                long count =
                    net_tcp_send(s->tcp, q->request + q->sent, q->request_length - q->sent);
                if (count > 0)
                    q->sent += count;
                else if (count != -EAGAIN) {
                    dns_finish(q, false, 0, 0);
                    continue;
                }
            }
            if (q->prefix_used < 2) {
                long count =
                    net_tcp_receive(s->tcp, q->prefix + q->prefix_used, 2 - q->prefix_used, false);
                if (count > 0)
                    q->prefix_used += count;
                else if (count != -EAGAIN) {
                    dns_finish(q, false, 0, 0);
                    continue;
                }
                if (q->prefix_used < 2)
                    continue;
                q->response_length = net_get16(q->prefix);
                if (q->response_length < 12) {
                    dns_finish(q, false, 0, 0);
                    continue;
                }
                q->response = kmalloc(q->response_length);
                if (!q->response) {
                    dns_finish(q, false, 0, 0);
                    continue;
                }
            }
            while (q->received < q->response_length) {
                long count = net_tcp_receive(s->tcp, q->response + q->received,
                                             q->response_length - q->received, false);
                if (count > 0)
                    q->received += count;
                else {
                    if (count != -EAGAIN)
                        dns_finish(q, false, 0, 0);
                    break;
                }
            }
            if (q->state == 1 && q->received == q->response_length) {
                uint8_t *response = q->response;
                q->response = NULL;
                dns_input(q, response, q->response_length, true);
                kfree(response);
                if (q->state == 1 && q->tcp)
                    dns_finish(q, false, 0, 0);
            }
        } else if (now >= q->next_send) {
            if (q->attempts >= 4) {
                dns_finish(q, false, 0, 0);
                continue;
            }
            if (q->attempts == 2 && dns_secondary)
                q->server = dns_secondary;
            long result =
                net_udp_send(-1, 0, q->server, q->port, 53, q->request + 2, q->request_length - 2);
            if (result == 0)
                q->attempts++;
            else if (result != -EAGAIN && result != -ENOMEM) {
                dns_finish(q, false, 0, 0);
                continue;
            }
            q->next_send = now + (1000ull << q->attempts);
        }
    }
}
