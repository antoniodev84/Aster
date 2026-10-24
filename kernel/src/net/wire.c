#include "core.h"

struct neighbor {
    unsigned interface, attempts;
    uint32_t address, sender;
    uint8_t mac[6];
    uint64_t expires, next_request;
    bool valid, used;
};
struct outgoing {
    struct outgoing *next;
    int interface;
    uint32_t source, destination, next_hop;
    bool broadcast;
    uint16_t id;
    uint8_t protocol;
    size_t length, offset;
    uint64_t expires;
    uint8_t data[];
};
struct reassembly {
    bool used, first;
    unsigned interface;
    uint32_t source, destination, next_hop;
    bool broadcast;
    uint16_t id;
    uint8_t protocol, header[60], header_length;
    uint8_t *data, *present;
    size_t total, received, high;
    uint64_t expires;
};
static struct neighbor neighbors[64];
static struct outgoing *out_head, *out_tail;
static unsigned out_count;
static size_t out_bytes;
static uint16_t next_id;
static struct reassembly fragments[16];
static uint64_t icmp_limit_time;
static unsigned icmp_count;
static void ip_input(unsigned interface, const uint8_t *packet, size_t length);
uint16_t net_checksum(const void *data, size_t length, uint32_t sum) {
    const uint8_t *p = data;
    while (length >= 2) {
        sum += net_get16(p);
        p += 2;
        length -= 2;
    }
    if (length)
        sum += (uint16_t)*p << 8;
    while (sum >> 16)
        sum = (sum & 65535) + (sum >> 16);
    return (uint16_t)~sum;
}
uint32_t net_pseudo(uint32_t source, uint32_t destination, uint8_t protocol, size_t length) {
    uint8_t addresses[8];
    memcpy(addresses, &source, 4);
    memcpy(addresses + 4, &destination, 4);
    return (uint16_t)~net_checksum(addresses, 8, 0) + protocol + length;
}
uint32_t net_source(int interface) {
    return interface == 4 ? net_ipv4(127, 0, 0, 1)
           : interface >= 0 && (unsigned)interface < ethernet_count
               ? ethernet_interfaces[interface].address
               : 0;
}
int net_route(uint32_t source, uint32_t destination) {
    if (((uint8_t *)&destination)[0] == 127) {
        if (source && ((uint8_t *)&source)[0] != 127)
            return -ENETUNREACH;
        return 4;
    }
    int best = -1;
    unsigned prefix = 0;
    for (unsigned i = 0; i < ethernet_count; i++) {
        struct net_interface *n = &ethernet_interfaces[i];
        if (!n->up || !n->link || (source && source != n->address))
            continue;
        if (n->address && (destination & n->netmask) == (n->address & n->netmask)) {
            unsigned bits = __builtin_popcount(n->netmask);
            if (best < 0 || bits > prefix || (bits == prefix && i == net_default_interface)) {
                best = i;
                prefix = bits;
            }
        }
    }
    if (best >= 0)
        return best;
    if (net_default_interface < ethernet_count) {
        struct net_interface *n = &ethernet_interfaces[net_default_interface];
        if (n->up && n->link && (!source || source == n->address) &&
            (n->gateway || destination == UINT32_MAX))
            return net_default_interface;
    }
    return -ENETUNREACH;
}
long net_ip_send(int interface, uint32_t source, uint32_t destination, uint8_t protocol,
                 const void *payload, size_t length) {
    if (length > 65515)
        return -EMSGSIZE;
    if (interface < 0)
        interface = net_route(source, destination);
    if (interface < 0)
        return interface;
    if (interface != 4 &&
        ((unsigned)interface >= ethernet_count || !ethernet_interfaces[interface].up ||
         !ethernet_interfaces[interface].link))
        return -ENETDOWN;
    if (out_count >= 256 || out_bytes + length > 2 * 1024 * 1024)
        return -EAGAIN;
    struct outgoing *p = kmalloc(sizeof(*p) + length);
    if (!p)
        return -ENOMEM;
    *p = (struct outgoing){.interface = interface,
                           .source = source ? source : net_source(interface),
                           .destination = destination,
                           .id = ++next_id,
                           .protocol = protocol,
                           .length = length,
                           .expires = hal_time_ms() + 10000};
    if (interface != 4) {
        struct net_interface *n = &ethernet_interfaces[interface];
        p->broadcast = destination == UINT32_MAX || (n->address && n->netmask != UINT32_MAX &&
                                                     destination == (n->address | ~n->netmask));
        p->next_hop =
            (destination & n->netmask) == (n->address & n->netmask) ? destination : n->gateway;
    }
    memcpy(p->data, payload, length);
    if (out_tail)
        out_tail->next = p;
    else
        out_head = p;
    out_tail = p;
    out_count++;
    out_bytes += length;
    return 0;
}
long net_udp_send(int interface, uint32_t source, uint32_t destination, uint16_t source_port,
                  uint16_t destination_port, const void *payload, size_t length) {
    if (length > 65507)
        return -EMSGSIZE;
    if (interface < 0)
        interface = net_route(source, destination);
    if (interface < 0)
        return interface;
    if (!source)
        source = net_source(interface);
    uint8_t *packet = kmalloc(length + 8);
    if (!packet)
        return -ENOMEM;
    net_put16(packet, source_port);
    net_put16(packet + 2, destination_port);
    net_put16(packet + 4, length + 8);
    net_put16(packet + 6, 0);
    memcpy(packet + 8, payload, length);
    uint16_t checksum =
        net_checksum(packet, length + 8, net_pseudo(source, destination, 17, length + 8));
    net_put16(packet + 6, checksum ? checksum : 65535);
    long result = net_ip_send(interface, source, destination, 17, packet, length + 8);
    kfree(packet);
    return result;
}
static void arp_packet(unsigned interface, unsigned operation, const uint8_t *mac, uint32_t source,
                       uint32_t target) {
    struct net_interface *n = &ethernet_interfaces[interface];
    uint8_t frame[42] = {0};
    if (operation == 1)
        memset(frame, 255, 6);
    else
        memcpy(frame, mac, 6);
    memcpy(frame + 6, n->mac, 6);
    net_put16(frame + 12, 0x806);
    net_put16(frame + 14, 1);
    net_put16(frame + 16, 0x800);
    frame[18] = 6;
    frame[19] = 4;
    net_put16(frame + 20, operation);
    memcpy(frame + 22, n->mac, 6);
    memcpy(frame + 28, &source, 4);
    if (operation == 2)
        memcpy(frame + 32, mac, 6);
    memcpy(frame + 38, &target, 4);
    ethernet_send(interface, frame, sizeof(frame));
}
static void arp_send(unsigned interface, unsigned operation, const uint8_t *mac, uint32_t target) {
    arp_packet(interface, operation, mac, ethernet_interfaces[interface].address, target);
}
void net_arp_probe(unsigned interface, uint32_t address, bool announce) {
    arp_packet(interface, 1, NULL, announce ? address : 0, address);
}
static struct neighbor *neighbor_get(unsigned interface, uint32_t address) {
    struct neighbor *oldest = &neighbors[0];
    uint64_t now = hal_time_ms();
    for (unsigned i = 0; i < 64; i++) {
        struct neighbor *n = &neighbors[i];
        if (n->used && n->interface == interface && n->address == address) {
            if (now >= n->expires) {
                n->valid = false;
                n->attempts = 0;
                n->next_request = 0;
                n->expires = now + 10000;
            }
            return n;
        }
        if (!n->used || n->expires < oldest->expires)
            oldest = n;
    }
    *oldest = (struct neighbor){
        .interface = interface, .address = address, .used = true, .expires = now + 10000};
    return oldest;
}
static void arp_input(unsigned interface, const uint8_t *p, size_t length) {
    if (length < 28 || net_get16(p) != 1 || net_get16(p + 2) != 0x800 || p[4] != 6 || p[5] != 4)
        return;
    unsigned operation = net_get16(p + 6);
    if (operation != 1 && operation != 2)
        return;
    uint32_t source = net_ip(p + 14), target = net_ip(p + 24);
    struct net_interface *n = &ethernet_interfaces[interface];
    if ((p[8] & 1) || !memcmp(p + 8, n->mac, 6))
        return;
    net_dhcp_conflict(interface, source, target);
    struct neighbor *pending = NULL;
    for (unsigned i = 0; i < 64; i++)
        if (neighbors[i].used && neighbors[i].interface == interface &&
            neighbors[i].address == source && !neighbors[i].valid &&
            neighbors[i].sender == target && !memcmp(p + 18, n->mac, 6))
            pending = &neighbors[i];
    if (source && ((target == n->address && n->address) || pending)) {
        struct neighbor *entry = pending ? pending : neighbor_get(interface, source);
        memcpy(entry->mac, p + 8, 6);
        entry->valid = true;
        entry->attempts = 0;
        entry->expires = hal_time_ms() + 60000;
    }
    if (operation == 1 && n->address && target == n->address)
        arp_send(interface, 2, p + 8, source);
}
static bool broadcast(unsigned interface, uint32_t address) {
    if (address == UINT32_MAX)
        return true;
    if (interface >= ethernet_count)
        return false;
    struct net_interface *n = &ethernet_interfaces[interface];
    return n->address && n->netmask != UINT32_MAX && address == (n->address | ~n->netmask);
}
static void icmp_error(unsigned interface, const uint8_t *ip, size_t length, uint8_t type,
                       uint8_t code) {
    uint32_t source = net_ip(ip + 12), destination = net_ip(ip + 16);
    if (!source || broadcast(interface, source) || broadcast(interface, destination) ||
        ((uint8_t *)&source)[0] >= 224 || (net_get16(ip + 6) & 0x1fff))
        return;
    size_t header = (ip[0] & 15) * 4;
    if (ip[9] == 1 && length > header && ip[header] != 0 && ip[header] != 8)
        return;
    uint64_t now = hal_time_ms();
    if (now - icmp_limit_time >= 1000) {
        icmp_limit_time = now;
        icmp_count = 0;
    }
    if (icmp_count++ >= 10)
        return;
    size_t copy = length < header + 8 ? length : header + 8;
    uint8_t message[76] = {type, code};
    memcpy(message + 8, ip, copy);
    net_put16(message + 2, net_checksum(message, copy + 8, 0));
    net_ip_send(interface, destination, source, 1, message, copy + 8);
}
static void transport_input(unsigned interface, const uint8_t *ip, size_t length) {
    size_t header = (ip[0] & 15) * 4;
    const uint8_t *p = ip + header;
    size_t size = length - header;
    uint32_t source = net_ip(ip + 12), destination = net_ip(ip + 16);
    if (interface < ethernet_count && destination != ethernet_interfaces[interface].address &&
        !broadcast(interface, destination) && (ip[9] != 17 || size < 8 || net_get16(p + 2) != 68))
        return;
    if (ip[9] == 1) {
        if (size < 8 || net_checksum(p, size, 0))
            return;
        for (struct net_socket *s = net_sockets; s; s = s->next)
            if (s->type == SOCK_RAW && (!s->local.address || s->local.address == destination) &&
                (!s->connected || s->peer.address == source))
                net_deliver(s, source, 0, ip, length);
        if (p[0] == 8 && !p[1] && !broadcast(interface, destination)) {
            uint8_t *reply = kmalloc(size);
            if (!reply)
                return;
            memcpy(reply, p, size);
            reply[0] = 0;
            reply[2] = reply[3] = 0;
            net_put16(reply + 2, net_checksum(reply, size, 0));
            net_ip_send(interface, destination, source, 1, reply, size);
            kfree(reply);
        } else if ((p[0] == 3 || p[0] == 11 || p[0] == 12) && size >= 36) {
            const uint8_t *quoted = p + 8;
            size_t qh = (quoted[0] & 15) * 4;
            if ((quoted[0] >> 4) != 4 || qh < 20 || size < 8 + qh + 8)
                return;
            for (struct net_socket *s = net_sockets; s; s = s->next) {
                if (s->type != SOCK_DGRAM || !s->connected || quoted[9] != 17)
                    continue;
                if (s->local.address == net_ip(quoted + 12) &&
                    s->peer.address == net_ip(quoted + 16) &&
                    net_swap16(s->local.port) == net_get16(quoted + qh) &&
                    net_swap16(s->peer.port) == net_get16(quoted + qh + 2)) {
                    s->error = p[0] == 3 && p[1] == 3 ? ECONNREFUSED
                               : p[0] == 11           ? ETIMEDOUT
                                                      : EHOSTUNREACH;
                    net_wake();
                }
            }
        }
    } else if (ip[9] == 17) {
        if (size < 8)
            return;
        size_t udp_length = net_get16(p + 4);
        if (udp_length < 8 || udp_length > size ||
            (net_get16(p + 6) &&
             net_checksum(p, udp_length, net_pseudo(source, destination, 17, udp_length))))
            return;
        uint16_t sport = net_get16(p), dport = net_get16(p + 2);
        if (net_services_udp(interface, source, sport, dport, p + 8, udp_length - 8))
            return;
        bool found = false;
        for (struct net_socket *s = net_sockets; s; s = s->next) {
            if (s->type != SOCK_DGRAM || net_swap16(s->local.port) != dport ||
                (s->local.address && s->local.address != destination) ||
                (s->connected && (s->peer.address != source || net_swap16(s->peer.port) != sport)))
                continue;
            net_deliver(s, source, sport, p + 8, udp_length - 8);
            found = true;
            if (!broadcast(interface, destination))
                break;
        }
        if (!found && !broadcast(interface, destination))
            icmp_error(interface, ip, length, 3, 3);
    } else if (ip[9] == 6) {
        if (broadcast(interface, destination))
            return;
        net_tcp_input(source, destination, p, size);
    } else if (!broadcast(interface, destination))
        icmp_error(interface, ip, length, 3, 2);
}
static void fragment_free(struct reassembly *r) {
    kfree(r->data);
    kfree(r->present);
    memset(r, 0, sizeof(*r));
}
static void fragment_input(unsigned interface, const uint8_t *ip, size_t length) {
    size_t header = (ip[0] & 15) * 4, count = length - header;
    uint16_t flags = net_get16(ip + 6), id = net_get16(ip + 4);
    size_t offset = (flags & 8191) * 8;
    if (!count || ((flags & 0x2000) && count % 8) || offset + count > 65515)
        return;
    uint32_t source = net_ip(ip + 12), destination = net_ip(ip + 16);
    struct reassembly *r = NULL, *empty = NULL;
    uint64_t now = hal_time_ms();
    for (unsigned i = 0; i < 16; i++) {
        struct reassembly *entry = &fragments[i];
        if (entry->used && now >= entry->expires)
            fragment_free(entry);
        if (!entry->used)
            empty = entry;
        else if (entry->interface == interface && entry->source == source &&
                 entry->destination == destination && entry->id == id && entry->protocol == ip[9])
            r = entry;
    }
    if (!r) {
        if (!empty)
            return;
        r = empty;
        r->data = kmalloc(65515);
        r->present = kcalloc(1, 8192);
        if (!r->data || !r->present) {
            fragment_free(r);
            return;
        }
        r->used = true;
        r->interface = interface;
        r->source = source;
        r->destination = destination;
        r->id = id;
        r->protocol = ip[9];
        r->total = SIZE_MAX;
        r->expires = now + 30000;
    }
    if (r->total != SIZE_MAX && offset + count > r->total) {
        fragment_free(r);
        return;
    }
    if (!(flags & 0x2000)) {
        if ((r->total != SIZE_MAX && r->total != offset + count) || r->high > offset + count) {
            fragment_free(r);
            return;
        }
        r->total = offset + count;
    }
    size_t overlap = 0;
    for (size_t i = 0; i < count; i++) {
        size_t pos = offset + i;
        if (r->present[pos / 8] & (1u << (pos % 8))) {
            if (r->data[pos] != ip[header + i]) {
                fragment_free(r);
                return;
            }
            overlap++;
        }
    }
    if (overlap && overlap != count) {
        fragment_free(r);
        return;
    }
    if (offset + count > r->high)
        r->high = offset + count;
    if (!overlap) {
        memcpy(r->data + offset, ip + header, count);
        for (size_t i = offset; i < offset + count; i++)
            r->present[i / 8] |= 1u << (i % 8);
        r->received += count;
    }
    if (!offset) {
        memcpy(r->header, ip, header);
        r->header_length = header;
        r->first = true;
    }
    if (r->first && r->total != SIZE_MAX && r->received == r->total) {
        if (r->total + r->header_length > 65535) {
            fragment_free(r);
            return;
        }
        uint8_t *packet = kmalloc(r->header_length + r->total);
        if (packet) {
            memcpy(packet, r->header, r->header_length);
            memcpy(packet + r->header_length, r->data, r->total);
            net_put16(packet + 2, r->header_length + r->total);
            net_put16(packet + 6, 0);
            net_put16(packet + 10, 0);
            net_put16(packet + 10, net_checksum(packet, r->header_length, 0));
            transport_input(interface, packet, r->header_length + r->total);
            kfree(packet);
        }
        fragment_free(r);
    }
}
static void ip_input(unsigned interface, const uint8_t *ip, size_t length) {
    if (length < 20 || (ip[0] >> 4) != 4)
        return;
    size_t header = (ip[0] & 15) * 4, size = net_get16(ip + 2);
    if (header < 20 || header > length || size < header || size > length || !ip[8] ||
        net_checksum(ip, header, 0) || (net_get16(ip + 6) & 0x8000))
        return;
    uint32_t destination = net_ip(ip + 16), source = net_ip(ip + 12);
    if (!source || ((uint8_t *)&source)[0] >= 224 || source == UINT32_MAX)
        return;
    if (interface == 4) {
        if (((uint8_t *)&destination)[0] != 127)
            return;
    } else {
        struct net_interface *n = &ethernet_interfaces[interface];
        bool dhcp = ip[9] == 17 && (!n->address || destination == UINT32_MAX);
        if (destination != n->address && !broadcast(interface, destination) && !dhcp)
            return;
        if (((uint8_t *)&source)[0] == 127 || ((uint8_t *)&destination)[0] == 127)
            return;
    }
    if (net_get16(ip + 6) & 0x3fff)
        fragment_input(interface, ip, size);
    else
        transport_input(interface, ip, size);
}
void net_ethernet_input(unsigned interface, const void *frame, size_t length) {
    if (interface >= ethernet_count || length < 14 || !ethernet_interfaces[interface].up)
        return;
    const uint8_t *p = frame;
    static const uint8_t all[6] = {255, 255, 255, 255, 255, 255};
    if (memcmp(p, all, 6) && memcmp(p, ethernet_interfaces[interface].mac, 6))
        return;
    if (net_get16(p + 12) == 0x806)
        arp_input(interface, p + 14, length - 14);
    else if (net_get16(p + 12) == 0x800)
        ip_input(interface, p + 14, length - 14);
}
void net_wire_poll(void) {
    uint64_t now = hal_time_ms();
    struct outgoing **link = &out_head;
    unsigned budget = 128;
    while (*link && budget) {
        struct outgoing *p = *link;
        bool drop = now >= p->expires;
        uint8_t mac[6];
        bool resolved = p->interface == 4;
        if (!drop && p->interface != 4) {
            struct net_interface *n = &ethernet_interfaces[p->interface];
            if (!n->up || !n->link)
                drop = true;
            else if (p->broadcast) {
                memset(mac, 255, 6);
                resolved = true;
            } else {
                uint32_t target = p->next_hop;
                if (!target)
                    drop = true;
                else {
                    struct neighbor *entry = neighbor_get(p->interface, target);
                    if (entry->valid) {
                        memcpy(mac, entry->mac, 6);
                        resolved = true;
                    } else if (now >= entry->next_request && entry->attempts < 5) {
                        entry->sender = p->source;
                        arp_packet(p->interface, 1, NULL, p->source, target);
                        entry->attempts++;
                        entry->next_request = now + 1000;
                    } else if (entry->attempts >= 5 && now >= entry->next_request)
                        drop = true;
                }
            }
        }
        while (!drop && resolved && budget) {
            size_t count = p->length - p->offset;
            if (count > 1480)
                count = 1480;
            uint8_t frame[1514] = {0};
            uint8_t *ip = frame + 14;
            ip[0] = 0x45;
            net_put16(ip + 2, count + 20);
            net_put16(ip + 4, p->id);
            net_put16(ip + 6, (p->offset / 8) | (p->offset + count < p->length ? 0x2000 : 0));
            ip[8] = 64;
            ip[9] = p->protocol;
            memcpy(ip + 12, &p->source, 4);
            memcpy(ip + 16, &p->destination, 4);
            net_put16(ip + 10, net_checksum(ip, 20, 0));
            memcpy(ip + 20, p->data + p->offset, count);
            if (p->interface == 4)
                ip_input(4, ip, count + 20);
            else {
                memcpy(frame, mac, 6);
                memcpy(frame + 6, ethernet_interfaces[p->interface].mac, 6);
                net_put16(frame + 12, 0x800);
                long result = ethernet_send(p->interface, frame, count + 34);
                if (result == -EAGAIN)
                    break;
                if (result < 0) {
                    drop = true;
                    break;
                }
            }
            budget--;
            p->offset += count;
            if (p->offset == p->length) {
                drop = true;
                break;
            }
        }
        if (drop) {
            *link = p->next;
            if (out_tail == p)
                out_tail = NULL;
            out_bytes -= p->length;
            out_count--;
            kfree(p);
        } else
            link = &p->next;
    }
    if (!out_head)
        out_tail = NULL;
    else if (!out_tail) {
        out_tail = out_head;
        while (out_tail->next)
            out_tail = out_tail->next;
    }
    for (unsigned i = 0; i < 16; i++)
        if (fragments[i].used && now >= fragments[i].expires)
            fragment_free(&fragments[i]);
}
