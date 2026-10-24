#include "netcommon.h"
static uint16_t checksum(const uint8_t *data, size_t length) {
    uint32_t sum = 0;
    while (length > 1) {
        sum += ((uint16_t)data[0] << 8) | data[1];
        data += 2;
        length -= 2;
    }
    if (length)
        sum += (uint16_t)*data << 8;
    while (sum >> 16)
        sum = (sum & 65535) + (sum >> 16);
    return (uint16_t)~sum;
}
int main(int argc, char **argv) {
    unsigned count = 4, timeout = 2, payload = 56;
    const char *host = NULL;
    for (int i = 1; i < argc; i++) {
        if ((!strcmp(argv[i], "-c") || !strcmp(argv[i], "-W") || !strcmp(argv[i], "-s")) &&
            i + 1 < argc) {
            char option = argv[i][1];
            unsigned *target = option == 'c' ? &count : option == 'W' ? &timeout : &payload;
            if (net_number(argv[++i], option == 's' ? 0 : 1,
                           option == 's'   ? 65507
                           : option == 'W' ? 86400
                                           : 65535,
                           target) < 0)
                goto usage;
        } else if (argv[i][0] == '-' || host)
            goto usage;
        else
            host = argv[i];
    }
    if (!host)
        goto usage;
    struct sockaddr_in peer = {.sin_family = AF_INET};
    if (netresolve(host, &peer.sin_addr) < 0) {
        perror("ping: resolve");
        return 1;
    }
    char address[16];
    inet_ntop(AF_INET, &peer.sin_addr, address, sizeof(address));
    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        perror("ping: socket");
        return 1;
    }
    uint8_t *request = malloc(payload + 8), *response = malloc(65535);
    if (!request || !response || net_timeout(fd, timeout * 1000) < 0) {
        perror("ping");
        free(request);
        free(response);
        close(fd);
        return 1;
    }
    uint16_t identifier = (uint16_t)getpid();
    unsigned received = 0, sent = 0;
    uint64_t total = 0, minimum = UINT64_MAX, maximum = 0;
    printf("PING %s (%s): %u data bytes\n", host, address, payload);
    for (unsigned sequence = 1; sequence <= count; sequence++) {
        memset(request, 0, payload + 8);
        request[0] = 8;
        request[4] = identifier >> 8;
        request[5] = identifier;
        request[6] = sequence >> 8;
        request[7] = sequence;
        for (unsigned j = 0; j < payload; j++)
            request[j + 8] = j;
        uint16_t check = checksum(request, payload + 8);
        request[2] = check >> 8;
        request[3] = check;
        uint64_t started = net_milliseconds(), deadline = started + timeout * 1000ull;
        if (sendto(fd, request, payload + 8, 0, (struct sockaddr *)&peer, sizeof(peer)) < 0) {
            perror("ping: send");
            break;
        }
        sent++;
        bool success = false;
        while (net_milliseconds() < deadline) {
            unsigned remaining = deadline - net_milliseconds();
            if (net_timeout(fd, remaining ? remaining : 1) < 0)
                break;
            struct sockaddr_in from;
            socklen_t length = sizeof(from);
            ssize_t n = recvfrom(fd, response, 65535, 0, (struct sockaddr *)&from, &length);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                if (errno != EAGAIN)
                    perror("ping: receive");
                break;
            }
            unsigned header = (response[0] & 15) * 4;
            if (n < 28 || (response[0] >> 4) != 4 || header < 20 || n < (ssize_t)header + 8)
                continue;
            uint8_t *icmp = response + header;
            if (icmp[0] || icmp[1] || from.sin_addr.s_addr != peer.sin_addr.s_addr ||
                icmp[4] != (uint8_t)(identifier >> 8) || icmp[5] != (uint8_t)identifier ||
                icmp[6] != (uint8_t)(sequence >> 8) || icmp[7] != (uint8_t)sequence ||
                checksum(icmp, n - header))
                continue;
            uint64_t elapsed = net_milliseconds() - started;
            printf("%ld bytes from %s: icmp_seq=%u ttl=%u time=%llu ms\n", n - header, address,
                   sequence, response[8], (unsigned long long)elapsed);
            if (elapsed < minimum)
                minimum = elapsed;
            if (elapsed > maximum)
                maximum = elapsed;
            total += elapsed;
            received++;
            success = true;
            break;
        }
        if (!success)
            printf("Request timeout for icmp_seq=%u\n", sequence);
        if (sequence < count && net_milliseconds() - started < 1000) {
            unsigned ms = 1000 - (net_milliseconds() - started);
            struct timespec delay = {ms / 1000, (ms % 1000) * 1000000};
            nanosleep(&delay, NULL);
        }
    }
    printf("--- %s ping statistics ---\n%u packets transmitted, %u received, %u%% packet loss\n",
           host, sent, received, sent ? (sent - received) * 100 / sent : 0);
    if (received)
        printf("round-trip min/avg/max = %llu/%llu/%llu ms\n", (unsigned long long)minimum,
               (unsigned long long)(total / received), (unsigned long long)maximum);
    free(request);
    free(response);
    close(fd);
    return received == sent && sent == count ? 0 : 1;
usage:
    fputs("Usage: ping [-c COUNT] [-W TIMEOUT_SECONDS] [-s PAYLOAD_BYTES] HOST\n", stderr);
    return 2;
}
