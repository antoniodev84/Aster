#include "core.h"
#define FIN 1u
#define SYN 2u
#define RST 4u
#define PSH 8u
#define ACK 16u
enum tcp_state {
    T_SYN_SENT,
    T_SYN_RECEIVED,
    T_ESTABLISHED,
    T_FIN_WAIT1,
    T_FIN_WAIT2,
    T_CLOSE_WAIT,
    T_CLOSING,
    T_LAST_ACK,
    T_TIME_WAIT,
    T_DEAD
};
struct tcp_segment {
    struct tcp_segment *next;
    uint32_t sequence;
    uint16_t length;
    uint8_t flags, retries;
    uint64_t sent;
    uint8_t data[];
};
struct tcp_connection {
    struct tcp_connection *next;
    struct net_socket *socket, *listener;
    enum tcp_state state;
    uint32_t local, remote, initial, peer_initial;
    uint16_t local_port, remote_port, mss;
    uint32_t send_unacknowledged, send_next, sent_high, receive_next, read_next;
    uint32_t window, window_sequence, window_ack, congestion, threshold;
    uint32_t smoothed_rtt, rtt_variation, retransmit;
    unsigned duplicate_acks, tx_bytes, available;
    struct tcp_segment *head, *tail;
    uint8_t *receive_data, *receive_bits;
    bool fin_known, eof, read_shutdown, write_shutdown, close_requested, fin_queued;
    uint32_t fin_sequence, own_fin;
    uint64_t updated, last_probe, deadline;
};
static struct tcp_connection *connections;
static unsigned connection_count;
static bool before(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}
static bool after(uint32_t a, uint32_t b) {
    return before(b, a);
}
static uint32_t segment_end(const struct tcp_segment *s) {
    return s->sequence + s->length + !!(s->flags & SYN) + !!(s->flags & FIN);
}
static uint16_t receive_window(struct tcp_connection *c) {
    if (c->read_shutdown)
        return NET_WINDOW;
    uint32_t used = c->receive_next - c->read_next;
    return used >= NET_WINDOW ? 0 : NET_WINDOW - used;
}
static long emit(struct tcp_connection *c, uint32_t sequence, uint8_t flags, const void *payload,
                 size_t length) {
    size_t header = flags & SYN ? 24 : 20;
    uint8_t *p = kmalloc(header + length);
    if (!p)
        return -ENOMEM;
    memset(p, 0, header);
    net_put16(p, c->local_port);
    net_put16(p + 2, c->remote_port);
    net_put32(p + 4, sequence);
    net_put32(p + 8, flags & ACK ? c->receive_next : 0);
    p[12] = (header / 4) << 4;
    p[13] = flags;
    net_put16(p + 14, receive_window(c));
    if (flags & SYN) {
        p[20] = 2;
        p[21] = 4;
        net_put16(p + 22, 1460);
    }
    if (length)
        memcpy(p + header, payload, length);
    net_put16(p + 16, net_checksum(p, header + length,
                                   net_pseudo(c->local, c->remote, 6, header + length)));
    long result = net_ip_send(-1, c->local, c->remote, 6, p, header + length);
    kfree(p);
    return result;
}
static void acknowledge(struct tcp_connection *c) {
    emit(c, c->sent_high, ACK, NULL, 0);
}
static void free_segments(struct tcp_connection *c) {
    while (c->head) {
        struct tcp_segment *s = c->head;
        c->head = s->next;
        kfree(s);
    }
    c->tail = NULL;
    c->tx_bytes = 0;
}
static void fail(struct tcp_connection *c, int error) {
    if (c->state == T_DEAD)
        return;
    if (c->socket) {
        c->socket->error = error;
        if (!error && c->available) {
            c->socket->eof = true;
            c->state = T_DEAD;
            c->eof = true;
            free_segments(c);
            net_wake();
            return;
        }
        c->socket->tcp = NULL;
        c->socket->connected = c->socket->connecting = false;
        if (!error)
            c->socket->eof = true;
        c->socket = NULL;
    }
    c->listener = NULL;
    c->state = T_DEAD;
    free_segments(c);
    net_wake();
}
static struct tcp_connection *allocate_connection(void) {
    if (connection_count >= 128)
        return NULL;
    struct tcp_connection *c = kcalloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->receive_data = kmalloc(NET_WINDOW);
    c->receive_bits = kcalloc(1, NET_WINDOW / 8);
    if (!c->receive_data || !c->receive_bits) {
        kfree(c->receive_data);
        kfree(c->receive_bits);
        kfree(c);
        return NULL;
    }
    c->mss = 1460;
    c->window = 65535;
    c->congestion = 14600;
    c->threshold = 65535;
    c->retransmit = 1000;
    c->updated = hal_time_ms();
    c->next = connections;
    connections = c;
    connection_count++;
    return c;
}
static long append_segment(struct tcp_connection *c, uint8_t flags, const void *data,
                           size_t length) {
    struct tcp_segment *s = kmalloc(sizeof(*s) + length);
    if (!s)
        return -ENOMEM;
    *s = (struct tcp_segment){.sequence = c->send_next, .flags = flags, .length = length};
    if (length)
        memcpy(s->data, data, length);
    if (c->tail)
        c->tail->next = s;
    else
        c->head = s;
    c->tail = s;
    c->send_next = segment_end(s);
    c->tx_bytes += length;
    return 0;
}
static void output(struct tcp_connection *c) {
    uint64_t now = hal_time_ms();
    for (struct tcp_segment *s = c->head; s; s = s->next) {
        if (s->sent)
            continue;
        uint32_t flight = c->sent_high - c->send_unacknowledged;
        uint32_t limit = c->window < c->congestion ? c->window : c->congestion;
        if (!(s->flags & SYN)) {
            if (flight >= limit)
                break;
            uint32_t allowance = limit - flight;
            if (s->length > allowance) {
                struct tcp_segment *rest = kmalloc(sizeof(*rest) + s->length - allowance);
                if (!rest)
                    break;
                *rest = (struct tcp_segment){.next = s->next,
                                             .sequence = s->sequence + allowance,
                                             .length = s->length - allowance,
                                             .flags = s->flags};
                memcpy(rest->data, s->data + allowance, rest->length);
                s->next = rest;
                s->length = allowance;
                s->flags &= ~PSH;
                if (c->tail == s)
                    c->tail = rest;
            }
            if (segment_end(s) - c->sent_high > allowance)
                break;
        }
        long result = emit(c, s->sequence, s->flags, s->data, s->length);
        if (result < 0) {
            if (result != -EAGAIN && result != -ENOMEM)
                fail(c, -result);
            break;
        }
        s->sent = now ? now : 1;
        if (after(segment_end(s), c->sent_high))
            c->sent_high = segment_end(s);
    }
}
long net_tcp_connect(struct net_socket *s) {
    for (struct tcp_connection *entry = connections; entry; entry = entry->next)
        if (entry->state != T_DEAD && entry->local == s->local.address &&
            entry->remote == s->peer.address && entry->local_port == net_swap16(s->local.port) &&
            entry->remote_port == net_swap16(s->peer.port))
            return -EADDRINUSE;
    struct tcp_connection *c = allocate_connection();
    if (!c)
        return -ENOMEM;
    c->socket = s;
    c->local = s->local.address;
    c->remote = s->peer.address;
    c->local_port = net_swap16(s->local.port);
    c->remote_port = net_swap16(s->peer.port);
    c->initial = net_random();
    c->send_unacknowledged = c->send_next = c->sent_high = c->initial;
    c->state = T_SYN_SENT;
    long error = append_segment(c, SYN, NULL, 0);
    if (error < 0) {
        fail(c, 0);
        return error;
    }
    s->tcp = c;
    s->connecting = true;
    output(c);
    return 0;
}
long net_tcp_send(struct tcp_connection *c, const void *data, size_t length) {
    if (!c || (c->state != T_ESTABLISHED && c->state != T_CLOSE_WAIT))
        return -ENOTCONN;
    if (c->write_shutdown || c->close_requested)
        return -EPIPE;
    size_t count = NET_WINDOW - c->tx_bytes;
    if (count > length)
        count = length;
    if (!count)
        return length ? -EAGAIN : 0;
    const uint8_t *p = data;
    size_t done = 0;
    while (done < count) {
        size_t chunk = count - done;
        if (chunk > c->mss)
            chunk = c->mss;
        long error = append_segment(c, ACK | PSH, p + done, chunk);
        if (error < 0) {
            if (!done)
                return error;
            break;
        }
        done += chunk;
    }
    output(c);
    return done;
}
long net_tcp_receive(struct tcp_connection *c, void *data, size_t length, bool peek) {
    if (!c)
        return -ENOTCONN;
    if (c->read_shutdown || !length)
        return 0;
    size_t count = c->available;
    if (count > length)
        count = length;
    if (!count)
        return c->eof ? 0 : -EAGAIN;
    uint8_t *p = data;
    for (size_t i = 0; i < count; i++)
        p[i] = c->receive_data[(c->read_next + i) & (NET_WINDOW - 1)];
    if (!peek) {
        for (size_t i = 0; i < count; i++) {
            unsigned pos = (c->read_next + i) & (NET_WINDOW - 1);
            c->receive_bits[pos / 8] &= ~(1u << (pos % 8));
        }
        c->read_next += count;
        c->available -= count;
        acknowledge(c);
    }
    return count;
}
static void request_fin(struct tcp_connection *c) {
    if (c->fin_queued || (c->state != T_ESTABLISHED && c->state != T_CLOSE_WAIT))
        return;
    c->own_fin = c->send_next;
    if (append_segment(c, ACK | FIN, NULL, 0) < 0)
        return;
    c->fin_queued = true;
    c->state = c->state == T_CLOSE_WAIT ? T_LAST_ACK : T_FIN_WAIT1;
    output(c);
}
void net_tcp_shutdown(struct tcp_connection *c, bool receive, bool send) {
    if (!c)
        return;
    if (receive) {
        c->read_shutdown = true;
        c->available = 0;
        c->read_next = c->receive_next;
        memset(c->receive_bits, 0, NET_WINDOW / 8);
        acknowledge(c);
    }
    if (send && !c->write_shutdown) {
        c->write_shutdown = c->close_requested = true;
        request_fin(c);
    }
}
void net_tcp_close(struct tcp_connection *c, bool discard) {
    if (!c)
        return;
    c->socket = NULL;
    if ((discard && c->available) || c->state == T_SYN_SENT || c->state == T_SYN_RECEIVED) {
        emit(c, c->sent_high, RST | ACK, NULL, 0);
        fail(c, 0);
        return;
    }
    c->read_shutdown = true;
    c->close_requested = c->write_shutdown = true;
    c->available = 0;
    c->read_next = c->receive_next;
    memset(c->receive_bits, 0, NET_WINDOW / 8);
    request_fin(c);
}
void net_tcp_detach_listener(struct net_socket *s) {
    for (struct tcp_connection *c = connections; c; c = c->next)
        if (c->listener == s) {
            emit(c, c->sent_high, RST | ACK, NULL, 0);
            fail(c, ECONNABORTED);
        }
}
bool net_tcp_port_used(uint32_t address, uint16_t port, bool reuse) {
    for (struct tcp_connection *c = connections; c; c = c->next)
        if (c->state != T_DEAD && c->local_port == port && (!address || c->local == address) &&
            !reuse)
            return true;
    return false;
}
static void update_rtt(struct tcp_connection *c, uint64_t sent) {
    uint32_t sample = hal_time_ms() - sent;
    if (!sample)
        sample = 1;
    if (!c->smoothed_rtt) {
        c->smoothed_rtt = sample;
        c->rtt_variation = sample / 2;
    } else {
        uint32_t delta =
            sample > c->smoothed_rtt ? sample - c->smoothed_rtt : c->smoothed_rtt - sample;
        c->rtt_variation = (3 * c->rtt_variation + delta) / 4;
        c->smoothed_rtt = (7 * c->smoothed_rtt + sample) / 8;
    }
    c->retransmit = c->smoothed_rtt + 4 * c->rtt_variation;
    if (c->retransmit < 200)
        c->retransmit = 200;
    if (c->retransmit > 60000)
        c->retransmit = 60000;
}
static void time_wait(struct tcp_connection *c) {
    c->state = T_TIME_WAIT;
    c->deadline = hal_time_ms() + 60000;
    free_segments(c);
}
static void process_ack(struct tcp_connection *c, uint32_t ack, uint16_t window, uint32_t seq) {
    if (after(ack, c->sent_high) || before(ack, c->send_unacknowledged))
        return;
    if (after(seq, c->window_sequence) ||
        (seq == c->window_sequence && !before(ack, c->window_ack))) {
        c->window = window;
        c->window_sequence = seq;
        c->window_ack = ack;
    }
    if (ack == c->send_unacknowledged) {
        if (c->head && c->head->length && ++c->duplicate_acks == 3) {
            struct tcp_segment *s = c->head;
            emit(c, s->sequence, s->flags, s->data, s->length);
            s->sent = hal_time_ms();
            s->retries++;
            c->threshold = c->congestion / 2;
            if (c->threshold < 2u * c->mss)
                c->threshold = 2u * c->mss;
            c->congestion = c->threshold + 3u * c->mss;
        }
        return;
    }
    uint32_t acknowledged = ack - c->send_unacknowledged;
    c->send_unacknowledged = ack;
    c->duplicate_acks = 0;
    if (c->congestion < c->threshold)
        c->congestion += acknowledged;
    else
        c->congestion += (uint32_t)((uint64_t)c->mss * acknowledged / c->congestion) + 1;
    if (c->congestion > 65535)
        c->congestion = 65535;
    while (c->head && !after(segment_end(c->head), ack)) {
        struct tcp_segment *s = c->head;
        if (s->sent && !s->retries)
            update_rtt(c, s->sent);
        c->tx_bytes -= s->length;
        c->head = s->next;
        kfree(s);
    }
    if (!c->head)
        c->tail = NULL;
    else if (before(c->head->sequence, ack) && c->head->length && !(c->head->flags & SYN)) {
        struct tcp_segment *s = c->head;
        unsigned count = ack - s->sequence;
        if (count > s->length)
            count = s->length;
        memmove(s->data, s->data + count, s->length - count);
        s->sequence += count;
        s->length -= count;
        c->tx_bytes -= count;
    }
    if (c->fin_queued && after(ack, c->own_fin)) {
        if (c->state == T_FIN_WAIT1) {
            c->state = T_FIN_WAIT2;
            c->deadline = hal_time_ms() + 120000;
        } else if (c->state == T_CLOSING)
            time_wait(c);
        else if (c->state == T_LAST_ACK)
            fail(c, 0);
    }
    net_wake();
}
static bool parse_options(const uint8_t *p, size_t header, uint16_t *mss) {
    size_t offset = 20;
    while (offset < header) {
        unsigned kind = p[offset++];
        if (!kind)
            break;
        if (kind == 1)
            continue;
        if (offset >= header)
            return false;
        unsigned size = p[offset++];
        if (size < 2 || size - 2 > header - offset)
            return false;
        if (kind == 2 && size == 4) {
            uint16_t value = net_get16(p + offset);
            if (!value)
                return false;
            *mss = value > 1460 ? 1460 : value;
        }
        offset += size - 2;
    }
    return true;
}
static void reset_unknown(uint32_t source, uint32_t destination, const uint8_t *p, size_t length,
                          size_t header) {
    if (p[13] & RST)
        return;
    struct tcp_connection temporary = {.local = destination,
                                       .remote = source,
                                       .local_port = net_get16(p + 2),
                                       .remote_port = net_get16(p)};
    if (p[13] & ACK)
        emit(&temporary, net_get32(p + 8), RST, NULL, 0);
    else {
        temporary.receive_next =
            net_get32(p + 4) + length - header + !!(p[13] & SYN) + !!(p[13] & FIN);
        emit(&temporary, 0, RST | ACK, NULL, 0);
    }
}
void net_tcp_input(uint32_t source, uint32_t destination, const uint8_t *p, size_t length) {
    if (length < 20 || net_checksum(p, length, net_pseudo(source, destination, 6, length)))
        return;
    size_t header = (p[12] >> 4) * 4;
    if (header < 20 || header > length || (p[12] & 0x0e))
        return;
    uint16_t mss = 536;
    if (!parse_options(p, header, &mss))
        return;
    uint16_t sport = net_get16(p), dport = net_get16(p + 2), window = net_get16(p + 14);
    uint32_t seq = net_get32(p + 4), ack = net_get32(p + 8);
    uint8_t flags = p[13];
    size_t size = length - header;
    struct tcp_connection *c = NULL;
    for (struct tcp_connection *entry = connections; entry; entry = entry->next)
        if (entry->state != T_DEAD && entry->local == destination && entry->remote == source &&
            entry->local_port == dport && entry->remote_port == sport) {
            c = entry;
            break;
        }
    if (!c) {
        struct net_socket *listener = NULL;
        for (struct net_socket *s = net_sockets; s; s = s->next)
            if (s->listening && net_swap16(s->local.port) == dport &&
                (!s->local.address || s->local.address == destination)) {
                listener = s;
                break;
            }
        if (!listener || !(flags & SYN) || (flags & (ACK | RST))) {
            reset_unknown(source, destination, p, length, header);
            return;
        }
        unsigned pending = listener->accepted_count;
        for (struct tcp_connection *entry = connections; entry; entry = entry->next)
            if (entry->listener == listener && entry->state == T_SYN_RECEIVED)
                pending++;
        if (pending >= listener->backlog)
            return;
        c = allocate_connection();
        if (!c)
            return;
        c->state = T_SYN_RECEIVED;
        c->listener = listener;
        c->local = destination;
        c->remote = source;
        c->local_port = dport;
        c->remote_port = sport;
        c->mss = mss;
        c->congestion = 10u * mss;
        if (c->congestion > 14600)
            c->congestion = 14600;
        c->window = window;
        c->initial = net_random();
        c->peer_initial = seq;
        c->receive_next = c->read_next = seq + 1;
        c->send_unacknowledged = c->send_next = c->sent_high = c->initial;
        if (append_segment(c, SYN | ACK, NULL, 0) < 0) {
            fail(c, ENOMEM);
            return;
        }
        output(c);
        return;
    }
    c->updated = hal_time_ms();
    if (c->state == T_TIME_WAIT) {
        if (!(flags & RST)) {
            acknowledge(c);
            if (flags & FIN)
                c->deadline = hal_time_ms() + 60000;
        }
        return;
    }
    bool simultaneous = false;
    if (c->state == T_SYN_SENT) {
        bool valid_ack = (flags & ACK) && ack == c->initial + 1;
        if (flags & RST) {
            if (valid_ack)
                fail(c, ECONNREFUSED);
            return;
        }
        if ((flags & ACK) && !valid_ack) {
            reset_unknown(source, destination, p, length, header);
            return;
        }
        if (!(flags & SYN))
            return;
        c->peer_initial = seq;
        c->receive_next = c->read_next = seq + 1;
        c->mss = mss;
        c->congestion = 10u * mss;
        if (c->congestion > 14600)
            c->congestion = 14600;
        c->window = window;
        if (valid_ack) {
            process_ack(c, ack, window, seq);
            c->state = T_ESTABLISHED;
            if (c->socket)
                c->socket->connected = true;
            acknowledge(c);
            net_wake();
            seq++;
        } else {
            c->state = T_SYN_RECEIVED;
            if (c->head) {
                c->head->flags = SYN | ACK;
                c->head->sent = 0;
            }
            output(c);
            return;
        }
    } else {
        if ((flags & SYN) && c->state == T_SYN_RECEIVED && seq == c->peer_initial) {
            if (c->socket && (flags & ACK) && ack == c->initial + 1) {
                flags &= ~SYN;
                seq++;
                simultaneous = true;
            } else {
                if (c->head)
                    emit(c, c->head->sequence, SYN | ACK, NULL, 0);
                return;
            }
        }
        uint32_t span = receive_window(c), consumed = size + !!(flags & FIN);
        bool acceptable = consumed
                              ? (span && before(seq, c->receive_next + span) &&
                                 after(seq + consumed, c->receive_next))
                              : seq == c->receive_next || (span && !before(seq, c->receive_next) &&
                                                           before(seq, c->receive_next + span));
        if (!acceptable) {
            if (!(flags & RST))
                acknowledge(c);
            return;
        }
        if (flags & RST) {
            if (seq == c->receive_next)
                fail(c, ECONNRESET);
            else
                acknowledge(c);
            return;
        }
        if (flags & SYN) {
            acknowledge(c);
            return;
        }
        if (!(flags & ACK))
            return;
        if (after(ack, c->sent_high)) {
            acknowledge(c);
            return;
        }
        if (c->state == T_SYN_RECEIVED) {
            if (ack != c->initial + 1) {
                reset_unknown(source, destination, p, length, header);
                return;
            }
            c->state = T_ESTABLISHED;
            if (c->listener) {
                c->socket = net_passive_socket(c->listener, c, c->local, c->local_port, c->remote,
                                               c->remote_port);
                c->listener = NULL;
                if (!c->socket) {
                    emit(c, c->sent_high, RST | ACK, NULL, 0);
                    fail(c, ENOMEM);
                    return;
                }
            } else if (c->socket)
                c->socket->connected = true;
            net_wake();
        }
        process_ack(c, ack, window, seq);
        if (simultaneous)
            acknowledge(c);
        if (c->state == T_DEAD)
            return;
    }
    if (size || (flags & FIN)) {
        uint32_t end = seq + size;
        if ((flags & FIN) && !c->fin_known && !before(end, c->receive_next) &&
            before(end, c->read_next + NET_WINDOW)) {
            c->fin_known = true;
            c->fin_sequence = end;
        }
        for (size_t i = 0; i < size; i++) {
            uint32_t position = seq + i;
            if (before(position, c->receive_next) || !before(position, c->read_next + NET_WINDOW) ||
                (c->fin_known && !before(position, c->fin_sequence)))
                continue;
            unsigned slot = position & (NET_WINDOW - 1);
            if (!(c->receive_bits[slot / 8] & (1u << (slot % 8)))) {
                c->receive_data[slot] = p[header + i];
                c->receive_bits[slot / 8] |= 1u << (slot % 8);
            }
        }
        while (before(c->receive_next, c->read_next + NET_WINDOW) &&
               (!c->fin_known || before(c->receive_next, c->fin_sequence))) {
            unsigned slot = c->receive_next & (NET_WINDOW - 1);
            if (!(c->receive_bits[slot / 8] & (1u << (slot % 8))))
                break;
            c->receive_next++;
            c->available++;
            if (c->read_shutdown) {
                c->receive_bits[slot / 8] &= ~(1u << (slot % 8));
                c->read_next++;
                c->available--;
            }
        }
        if (c->fin_known && c->receive_next == c->fin_sequence) {
            c->receive_next++;
            c->eof = true;
            if (c->socket)
                c->socket->eof = true;
            if (c->state == T_ESTABLISHED)
                c->state = T_CLOSE_WAIT;
            else if (c->state == T_FIN_WAIT1)
                c->state = T_CLOSING;
            else if (c->state == T_FIN_WAIT2)
                time_wait(c);
        }
        acknowledge(c);
        net_wake();
    }
    output(c);
}
void net_tcp_poll(void) {
    uint64_t now = hal_time_ms();
    struct tcp_connection **link = &connections;
    while (*link) {
        struct tcp_connection *c = *link;
        if (c->state == T_TIME_WAIT && now >= c->deadline) {
            if (c->socket) {
                c->socket->eof = true;
                c->eof = true;
                if (!c->available) {
                    c->socket->tcp = NULL;
                    c->socket = NULL;
                }
            }
            c->state = T_DEAD;
        }
        if (c->state == T_FIN_WAIT2 && now >= c->deadline)
            fail(c, ETIMEDOUT);
        if (c->state == T_DEAD && c->socket) {
            link = &c->next;
            continue;
        }
        if (c->state == T_DEAD) {
            *link = c->next;
            free_segments(c);
            kfree(c->receive_data);
            kfree(c->receive_bits);
            kfree(c);
            connection_count--;
            continue;
        }
        if (c->close_requested && !c->fin_queued)
            request_fin(c);
        struct tcp_segment *s = c->head;
        if (s && s->sent && now >= s->sent + c->retransmit) {
            if (s->retries >= 8) {
                emit(c, c->sent_high, RST | ACK, NULL, 0);
                fail(c, ETIMEDOUT);
            } else if (emit(c, s->sequence, s->flags, s->data, s->length) == 0) {
                s->sent = now;
                s->retries++;
                c->threshold = (c->sent_high - c->send_unacknowledged) / 2;
                if (c->threshold < 2u * c->mss)
                    c->threshold = 2u * c->mss;
                c->congestion = c->mss;
                c->retransmit *= 2;
                if (c->retransmit > 60000)
                    c->retransmit = 60000;
            }
        }
        if (c->head && !c->window && now >= c->last_probe + 1000) {
            uint8_t byte = 0;
            emit(c, c->send_unacknowledged - 1, ACK, &byte, 1);
            c->last_probe = now;
        }
        if (c->state != T_DEAD)
            output(c);
        link = &c->next;
    }
}
