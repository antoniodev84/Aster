#include "core.h"
struct net_socket *net_sockets;
unsigned net_default_interface;
static unsigned socket_count;
static bool ready;
static uint64_t random_state;
uint32_t net_random(void) {
    random_state ^= hal_counter_ns() + 0x9e3779b97f4a7c15ull;
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state ^ (random_state >> 32);
}
void net_wake(void) {
    for (struct process *p = process_list(); p; p = p->next)
        if (p->state == PROCESS_BLOCKED && p->block_reason == BLOCK_NET)
            p->state = PROCESS_RUNNABLE;
}
struct net_socket *net_socket_allocate(int type) {
    if (socket_count >= 256)
        return NULL;
    struct net_socket *s = kcalloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->type = type;
    s->local.family = s->peer.family = AF_INET;
    s->next = net_sockets;
    net_sockets = s;
    socket_count++;
    return s;
}
void net_close(struct net_socket *s) {
    if (!s)
        return;
    if (s->listening)
        net_tcp_detach_listener(s);
    if (s->tcp)
        net_tcp_close(s->tcp, true);
    for (unsigned i = 0; i < s->accepted_count; i++)
        net_close(s->accepted[i]);
    while (s->head) {
        struct net_datagram *d = s->head;
        s->head = d->next;
        kfree(d);
    }
    struct net_socket **link = &net_sockets;
    while (*link && *link != s)
        link = &(*link)->next;
    if (*link) {
        *link = s->next;
        socket_count--;
    }
    kfree(s);
}
void net_deliver(struct net_socket *s, uint32_t source, uint16_t port, const void *data,
                 size_t length) {
    if (s->read_shutdown || s->queued >= 32)
        return;
    struct net_datagram *d = kmalloc(sizeof(*d) + length);
    if (!d)
        return;
    *d = (struct net_datagram){.length = length, .peer = {AF_INET, net_swap16(port), source, {0}}};
    memcpy(d->data, data, length);
    if (s->tail)
        s->tail->next = d;
    else
        s->head = d;
    s->tail = d;
    s->queued++;
    net_wake();
}
struct net_socket *net_passive_socket(struct net_socket *listener,
                                      struct tcp_connection *connection, uint32_t local,
                                      uint16_t lport, uint32_t peer, uint16_t rport) {
    if (!listener->listening || listener->accepted_count >= listener->backlog)
        return NULL;
    struct net_socket *s = net_socket_allocate(SOCK_STREAM);
    if (!s)
        return NULL;
    s->local = (struct abi_sockaddr_in){AF_INET, net_swap16(lport), local, {0}};
    s->peer = (struct abi_sockaddr_in){AF_INET, net_swap16(rport), peer, {0}};
    s->connected = s->bound = true;
    s->tcp = connection;
    listener->accepted[listener->accepted_count++] = s;
    return s;
}
static bool port_used(int type, uint32_t address, uint16_t port, bool reuse,
                      struct net_socket *self) {
    if (type == SOCK_DGRAM && net_services_port_used(port))
        return true;
    for (struct net_socket *s = net_sockets; s; s = s->next) {
        if (s == self || s->type != type || net_swap16(s->local.port) != port || !s->local.port)
            continue;
        if (s->local.address && address && s->local.address != address)
            continue;
        if (type == SOCK_DGRAM && reuse && s->reuse)
            continue;
        if (type == SOCK_STREAM && !s->listening && s->connected)
            continue;
        return true;
    }
    return type == SOCK_STREAM && net_tcp_port_used(address, port, reuse);
}
uint16_t net_ephemeral(int type, uint32_t address) {
    static uint16_t next;
    if (!next)
        next = 49152 + (net_random() % 16384);
    for (unsigned i = 0; i < 16384; i++) {
        uint16_t port = next++;
        if (next < 49152)
            next = 49152;
        if (!port_used(type, address, port, false, NULL))
            return port;
    }
    return 0;
}
static long socket_receive(struct net_socket *s, void *buffer, size_t length, unsigned flags,
                           struct abi_sockaddr_in *peer) {
    if (s->listening)
        return -ENOTCONN;
    if (s->read_shutdown)
        return 0;
    if (s->type == SOCK_STREAM) {
        if (!length)
            return 0;
        if (s->tcp) {
            long result = net_tcp_receive(s->tcp, buffer, length, flags & MSG_PEEK);
            if (result >= 0 && peer)
                *peer = s->peer;
            return result;
        }
        if (s->error)
            return -s->error;
        return s->eof ? 0 : -ENOTCONN;
    }
    if (!s->head)
        return s->error ? -s->error : -EAGAIN;
    struct net_datagram *d = s->head;
    size_t count = d->length < length ? d->length : length;
    memcpy(buffer, d->data, count);
    if (peer)
        *peer = d->peer;
    if (!(flags & MSG_PEEK)) {
        s->head = d->next;
        if (!s->head)
            s->tail = NULL;
        s->queued--;
        kfree(d);
    }
    return count;
}
static long socket_send(struct net_socket *s, const void *data, size_t length,
                        const struct abi_sockaddr_in *destination) {
    if (s->write_shutdown)
        return -EPIPE;
    if (s->error)
        return -s->error;
    if (s->type == SOCK_STREAM)
        return net_tcp_send(s->tcp, data, length);
    if (length > (s->type == SOCK_RAW ? 65515u : 65507u))
        return -EMSGSIZE;
    if (!destination) {
        if (!s->connected)
            return -EDESTADDRREQ;
        destination = &s->peer;
    }
    if (!destination->address || (s->type == SOCK_DGRAM && !destination->port))
        return -EINVAL;
    int interface = net_route(s->local.address, destination->address);
    if (interface < 0)
        return interface;
    bool broadcast = destination->address == UINT32_MAX;
    if (interface != 4) {
        struct net_interface *n = &ethernet_interfaces[interface];
        if (n->netmask != UINT32_MAX && destination->address == (n->address | ~n->netmask))
            broadcast = true;
    }
    if (broadcast && !s->broadcast)
        return -EACCES;
    if (s->type == SOCK_DGRAM && !s->local.port) {
        uint16_t port = net_ephemeral(s->type, s->local.address);
        if (!port)
            return -EADDRINUSE;
        s->local.port = net_swap16(port);
        s->bound = true;
    }
    long result =
        s->type == SOCK_DGRAM
            ? net_udp_send(interface, s->local.address, destination->address,
                           net_swap16(s->local.port), net_swap16(destination->port), data, length)
            : net_ip_send(interface, s->local.address, destination->address, 1, data, length);
    return result < 0 ? result : (long)length;
}
long net_read(struct net_socket *s, void *buffer, size_t size) {
    return socket_receive(s, buffer, size, 0, NULL);
}
long net_write(struct net_socket *s, const void *buffer, size_t size) {
    return socket_send(s, buffer, size, NULL);
}
static long install_socket(struct net_socket *s, unsigned flags) {
    struct process *p = process_current();
    for (unsigned i = 0; i < FD_MAX; i++) {
        if (p->fds[i].file)
            continue;
        struct open_file *f = file_new(NULL, O_RDWR | (flags & O_NONBLOCK));
        if (!f)
            return -ENOMEM;
        f->socket = s;
        p->fds[i] = (struct descriptor){f, flags & O_CLOEXEC ? FD_CLOEXEC : 0};
        return i;
    }
    return -EMFILE;
}
static long address_in(uint64_t pointer, uint64_t length, struct abi_sockaddr_in *address) {
    if (length < sizeof(*address))
        return -EINVAL;
    if (!copy_from_user(process_current()->space, address, pointer, sizeof(*address)))
        return -EFAULT;
    return address->family == AF_INET ? 0 : -EAFNOSUPPORT;
}
static long address_validate(uint64_t pointer, uint64_t length_pointer) {
    if (!pointer)
        return 0;
    uint32_t length;
    struct address_space *space = process_current()->space;
    if (!copy_from_user(space, &length, length_pointer, sizeof(length)) ||
        !user_range(space, length_pointer, sizeof(length), true) ||
        !user_range(space, pointer, length < 16 ? length : 16, true))
        return -EFAULT;
    return 0;
}
static void address_out(uint64_t pointer, uint64_t length_pointer,
                        const struct abi_sockaddr_in *address) {
    if (!pointer)
        return;
    uint32_t length;
    struct address_space *space = process_current()->space;
    copy_from_user(space, &length, length_pointer, sizeof(length));
    if (length > sizeof(*address))
        length = sizeof(*address);
    copy_to_user(space, pointer, address, length);
    length = sizeof(*address);
    copy_to_user(space, length_pointer, &length, sizeof(length));
}
static long wait_result(struct open_file *file, long result, unsigned flags, uint64_t operation,
                        bool sending) {
    struct process *p = process_current();
    if (result != -EAGAIN || (file->flags & O_NONBLOCK) || (flags & MSG_DONTWAIT)) {
        p->net_deadline = p->net_operation = 0;
        return result;
    }
    uint64_t timeout = sending ? file->socket->send_timeout : file->socket->receive_timeout;
    if (p->net_operation != operation) {
        p->net_operation = operation;
        p->net_deadline = timeout ? hal_time_ms() + timeout : 0;
    }
    if (p->net_deadline && hal_time_ms() >= p->net_deadline) {
        p->net_deadline = p->net_operation = 0;
        return -EAGAIN;
    }
    return process_block(BLOCK_NET);
}
static long resolve(uint64_t pointer, uint64_t output) {
    struct process *p = process_current();
    char name[256];
    long result = user_cstring(p->space, name, pointer, sizeof(name));
    if (result < 0)
        return result;
    if (!user_range(p->space, output, 4, true))
        return -EFAULT;
    uint32_t address;
    result = net_dns_lookup(name, &address);
    if (result == -EAGAIN)
        return process_block(BLOCK_NET);
    if (!result)
        copy_to_user(p->space, output, &address, 4);
    return result;
}
static long configure(uint64_t pointer, bool set) {
    struct process *p = process_current();
    struct abi_netconfig config;
    if (!copy_from_user(p->space, &config, pointer, sizeof(config)))
        return -EFAULT;
    if (config.index >= ethernet_count)
        return -ENODEV;
    struct net_interface *n = &ethernet_interfaces[config.index];
    if (set) {
        if (p->euid)
            return -EPERM;
        uint32_t inverse = ~__builtin_bswap32(config.netmask);
        if (inverse & (inverse + 1))
            return -EINVAL;
        uint8_t first = ((uint8_t *)&config.address)[0];
        if (config.address &&
            (!first || first == 127 || first >= 224 || config.address == UINT32_MAX))
            return -EINVAL;
        uint32_t host = config.address & ~config.netmask;
        if (config.address && inverse > 1 && (!host || host == ~config.netmask))
            return -EINVAL;
        if (n->address != config.address || n->netmask != config.netmask ||
            n->gateway != config.gateway || !(config.flags & NETCFG_UP))
            net_dhcp_stop(config.index);
        n->address = config.address;
        n->netmask = config.netmask;
        n->gateway = config.gateway;
        n->up = !!(config.flags & NETCFG_UP);
        if (config.flags & NETCFG_DEFAULT)
            net_default_interface = config.index;
        net_dns_server = config.dns;
        return 0;
    }
    if (!user_range(p->space, pointer, sizeof(config), true))
        return -EFAULT;
    config.flags = (n->up ? NETCFG_UP : 0) | (n->link ? NETCFG_LINK : 0) |
                   (net_dhcp_bound(config.index) ? NETCFG_DHCP : 0) |
                   (net_default_interface == config.index ? NETCFG_DEFAULT : 0);
    config.address = n->address;
    config.netmask = n->netmask;
    config.gateway = n->gateway;
    config.dns = net_dns_server;
    memcpy(config.mac, n->mac, 6);
    config.mtu = 1500;
    memset(config.name, 0, sizeof(config.name));
    kstrlcpy(config.name, "en", sizeof(config.name));
    knumber(config.name, sizeof(config.name), config.index + 1, 10);
    copy_to_user(p->space, pointer, &config, sizeof(config));
    return 0;
}
static bool local_address(uint32_t address) {
    if (!address || ((uint8_t *)&address)[0] == 127)
        return true;
    for (unsigned i = 0; i < ethernet_count; i++)
        if (ethernet_interfaces[i].address == address)
            return true;
    return false;
}
long net_syscall(uint64_t number, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,
                 uint64_t f) {
    struct process *p = process_current();
    if (number == SYS_netconfig)
        return configure(a, b != 0);
    if (number == SYS_resolve)
        return resolve(a, b);
    if (number == SYS_dhcp) {
        if (b != 1 && p->euid)
            return -EPERM;
        if (a >= ethernet_count || b > 3)
            return a >= ethernet_count ? -ENODEV : -EINVAL;
        return net_dhcp(a, b);
    }
    if (number == SYS_socket) {
        if (a != AF_INET)
            return -EAFNOSUPPORT;
        if (b > UINT32_MAX || c > 255)
            return -EINVAL;
        unsigned type = b & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_RAW)
            return -EINVAL;
        if ((type == SOCK_STREAM && c && c != IPPROTO_TCP) ||
            (type == SOCK_DGRAM && c && c != IPPROTO_UDP) ||
            (type == SOCK_RAW && c != IPPROTO_ICMP))
            return -EPROTONOSUPPORT;
        if (type == SOCK_RAW && p->euid)
            return -EPERM;
        struct net_socket *s = net_socket_allocate(type);
        if (!s)
            return -ENOMEM;
        long fd = install_socket(s, b);
        if (fd < 0)
            net_close(s);
        return fd;
    }
    if (a >= FD_MAX || !p->fds[a].file)
        return -EBADF;
    struct open_file *file = p->fds[a].file;
    struct net_socket *s = file->socket;
    if (!s)
        return -ENOTSOCK;
    if (number == SYS_bind || number == SYS_connect) {
        struct abi_sockaddr_in address;
        long result = address_in(b, c, &address);
        if (result < 0)
            return result;
        if (number == SYS_bind) {
            if (s->bound || s->connected || s->connecting || s->listening)
                return -EINVAL;
            if (!local_address(address.address))
                return -EADDRNOTAVAIL;
            uint16_t port = net_swap16(address.port);
            if (s->type != SOCK_RAW) {
                if (port && port < 1024 && p->euid)
                    return -EACCES;
                if (!port) {
                    port = net_ephemeral(s->type, address.address);
                    if (!port)
                        return -EADDRINUSE;
                }
                if (port_used(s->type, address.address, port, s->reuse, s))
                    return -EADDRINUSE;
                address.port = net_swap16(port);
            }
            s->local = address;
            s->bound = true;
            return 0;
        }
        if (s->type == SOCK_STREAM) {
            if (s->error)
                return -s->error;
            if (s->listening)
                return -EINVAL;
            if (s->connected) {
                if (!s->connecting)
                    return -EISCONN;
                s->connecting = false;
                p->net_deadline = p->net_operation = 0;
                return 0;
            }
            if (s->connecting) {
                if (file->flags & O_NONBLOCK)
                    return -EALREADY;
                return wait_result(file, -EAGAIN, 0, (number << 32) | a, true);
            }
        }
        if (!address.address || (s->type != SOCK_RAW && !address.port))
            return -EINVAL;
        int interface = net_route(s->local.address, address.address);
        if (interface < 0)
            return interface;
        if (!s->local.address)
            s->local.address = net_source(interface);
        if (s->type != SOCK_RAW && !s->local.port) {
            uint16_t port = net_ephemeral(s->type, s->local.address);
            if (!port)
                return -EADDRINUSE;
            s->local.port = net_swap16(port);
        }
        s->bound = true;
        s->peer = address;
        if (s->type != SOCK_STREAM) {
            s->connected = true;
            return 0;
        }
        result = net_tcp_connect(s);
        if (result < 0)
            return result;
        if (file->flags & O_NONBLOCK)
            return -EINPROGRESS;
        return wait_result(file, -EAGAIN, 0, (number << 32) | a, true);
    }
    if (number == SYS_listen) {
        if (s->type != SOCK_STREAM)
            return -ENOTSUP;
        if (s->connected || s->connecting || s->tcp)
            return -EINVAL;
        if (!s->local.port) {
            uint16_t port = net_ephemeral(s->type, s->local.address);
            if (!port)
                return -EADDRINUSE;
            s->local.port = net_swap16(port);
            s->bound = true;
        }
        s->backlog = b > 16 ? 16 : b ? b : 1;
        s->listening = true;
        return 0;
    }
    if (number == SYS_accept) {
        if (!s->listening)
            return -EINVAL;
        long result = address_validate(b, c);
        if (result < 0)
            return result;
        if (!s->accepted_count)
            return wait_result(file, -EAGAIN, 0, (number << 32) | a, false);
        struct net_socket *child = s->accepted[0];
        long fd = install_socket(child, 0);
        if (fd < 0)
            return fd;
        memmove(s->accepted, s->accepted + 1, (--s->accepted_count) * sizeof(child));
        address_out(b, c, &child->peer);
        p->net_deadline = p->net_operation = 0;
        return fd;
    }
    if (number == SYS_sendto || number == SYS_recvfrom) {
        bool send = number == SYS_sendto;
        if ((d & ~(MSG_PEEK | MSG_DONTWAIT | MSG_NOSIGNAL)) || (send && (d & MSG_PEEK)))
            return -ENOTSUP;
        if (c > 65535) {
            if (s->type != SOCK_STREAM)
                return -EMSGSIZE;
            c = 65535;
        }
        if (!user_range(p->space, b, c, !send))
            return -EFAULT;
        struct abi_sockaddr_in address = {.family = AF_INET};
        long result = 0;
        if (send && e)
            result = address_in(e, f, &address);
        else if (!send)
            result = address_validate(e, f);
        if (result < 0)
            return result;
        void *buffer = kmalloc(c ? c : 1);
        if (!buffer)
            return -ENOMEM;
        if (send) {
            copy_from_user(p->space, buffer, b, c);
            result = socket_send(s, buffer, c, e ? &address : NULL);
        } else {
            result = socket_receive(s, buffer, c, d, &address);
            if (result >= 0) {
                copy_to_user(p->space, b, buffer, result);
                address_out(e, f, &address);
            }
        }
        kfree(buffer);
        if (send && result == -EPIPE && !(d & MSG_NOSIGNAL))
            process_signal(p, SIGPIPE);
        return wait_result(file, result, d, (number << 32) | a, send);
    }
    if (number == SYS_shutdown) {
        if (b > SHUT_RDWR)
            return -EINVAL;
        if (!s->connected || s->listening)
            return -ENOTCONN;
        if (s->tcp)
            net_tcp_shutdown(s->tcp, b != SHUT_WR, b != SHUT_RD);
        if (b != SHUT_WR)
            s->read_shutdown = true;
        if (b != SHUT_RD)
            s->write_shutdown = true;
        return 0;
    }
    if (number == SYS_getsockname || number == SYS_getpeername) {
        if (!b)
            return -EFAULT;
        long result = address_validate(b, c);
        if (result < 0)
            return result;
        bool peer = number == SYS_getpeername;
        if (peer && !s->connected)
            return -ENOTCONN;
        address_out(b, c, peer ? &s->peer : &s->local);
        return 0;
    }
    if (number == SYS_setsockopt || number == SYS_getsockopt) {
        if (b != SOL_SOCKET)
            return -ENOPROTOOPT;
        bool set = number == SYS_setsockopt;
        uint32_t length;
        if (set)
            length = e;
        else if (!copy_from_user(p->space, &length, e, 4) || !user_range(p->space, e, 4, true))
            return -EFAULT;
        if (c == SO_RCVTIMEO || c == SO_SNDTIMEO) {
            if (length < sizeof(struct abi_timespec))
                return -EINVAL;
            uint64_t *value = c == SO_RCVTIMEO ? &s->receive_timeout : &s->send_timeout;
            struct abi_timespec timeout;
            if (set) {
                if (!copy_from_user(p->space, &timeout, d, sizeof(timeout)))
                    return -EFAULT;
                if (timeout.tv_sec < 0 || timeout.tv_sec > 86400 || timeout.tv_nsec < 0 ||
                    timeout.tv_nsec >= 1000000)
                    return -EINVAL;
                *value = timeout.tv_sec * 1000 + (timeout.tv_nsec + 999) / 1000;
            } else {
                timeout = (struct abi_timespec){*value / 1000, (*value % 1000) * 1000};
                if (!copy_to_user(p->space, d, &timeout, sizeof(timeout)))
                    return -EFAULT;
                length = sizeof(timeout);
                copy_to_user(p->space, e, &length, 4);
            }
            return 0;
        }
        if (length < 4)
            return -EINVAL;
        int value;
        if (set) {
            if (!copy_from_user(p->space, &value, d, 4))
                return -EFAULT;
            if (c == SO_REUSEADDR)
                s->reuse = value != 0;
            else if (c == SO_BROADCAST)
                s->broadcast = value != 0;
            else
                return -ENOPROTOOPT;
        } else {
            if (c == SO_ERROR)
                value = s->error;
            else if (c == SO_REUSEADDR)
                value = s->reuse;
            else if (c == SO_BROADCAST)
                value = s->broadcast;
            else
                return -ENOPROTOOPT;
            if (!copy_to_user(p->space, d, &value, 4))
                return -EFAULT;
            length = 4;
            copy_to_user(p->space, e, &length, 4);
            if (c == SO_ERROR)
                s->error = 0;
        }
        return 0;
    }
    return -ENOSYS;
}
void net_init(void) {
    ethernet_init();
    ready = true;
    ethernet_poll();
}
void net_poll(void) {
    if (!ready)
        return;
    ethernet_poll();
    net_services_poll();
    net_tcp_poll();
    net_wire_poll();
    net_wake();
}
