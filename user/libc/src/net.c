#include <net.h>
_Static_assert(sizeof(struct sockaddr_in) == sizeof(struct abi_sockaddr_in), "IPv4 socket ABI");
uint16_t htons(uint16_t value) {
    return __builtin_bswap16(value);
}
uint16_t ntohs(uint16_t value) {
    return htons(value);
}
uint32_t htonl(uint32_t value) {
    return __builtin_bswap32(value);
}
uint32_t ntohl(uint32_t value) {
    return htonl(value);
}
int inet_pton(int family, const char *text, void *address) {
    if (family != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (!text || !address) {
        errno = EFAULT;
        return -1;
    }
    uint8_t bytes[4];
    for (unsigned i = 0; i < 4; i++) {
        if (*text < '0' || *text > '9')
            return 0;
        unsigned value = 0, digits = 0;
        do {
            value = value * 10 + (unsigned)(*text++ - '0');
            if (++digits > 3 || value > 255)
                return 0;
        } while (*text >= '0' && *text <= '9');
        bytes[i] = value;
        if (i < 3 && *text++ != '.')
            return 0;
    }
    if (*text)
        return 0;
    memcpy(address, bytes, 4);
    return 1;
}
const char *inet_ntop(int family, const void *address, char *text, socklen_t size) {
    if (family != AF_INET) {
        errno = EAFNOSUPPORT;
        return NULL;
    }
    if (!address || !text) {
        errno = EFAULT;
        return NULL;
    }
    const uint8_t *a = address;
    char temporary[16];
    int length = snprintf(temporary, sizeof(temporary), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    if (length < 0 || (unsigned)length >= size) {
        errno = ENOSPC;
        return NULL;
    }
    memcpy(text, temporary, length + 1);
    return text;
}
in_addr_t inet_addr(const char *text) {
    in_addr_t address;
    return inet_pton(AF_INET, text, &address) == 1 ? address : INADDR_BROADCAST;
}
char *inet_ntoa(struct in_addr address) {
    static char text[16];
    return (char *)inet_ntop(AF_INET, &address, text, sizeof(text));
}
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **result) {
    if (!result)
        return EAI_FAIL;
    *result = NULL;
    if (!node && !service)
        return EAI_NONAME;
    if (hints && hints->ai_family && hints->ai_family != AF_INET)
        return EAI_FAMILY;
    int flags = hints ? hints->ai_flags : 0;
    if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV))
        return EAI_BADFLAGS;
    int type = hints && hints->ai_socktype ? hints->ai_socktype : SOCK_STREAM;
    if (type != SOCK_STREAM && type != SOCK_DGRAM)
        return EAI_SOCKTYPE;
    int protocol = type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
    if (hints && hints->ai_protocol && hints->ai_protocol != protocol)
        return EAI_SOCKTYPE;
    unsigned port = 0;
    if (service) {
        if (!*service)
            return EAI_SERVICE;
        for (const char *s = service; *s; s++) {
            if (*s < '0' || *s > '9' || port > 6553)
                return EAI_SERVICE;
            port = port * 10 + (unsigned)(*s - '0');
        }
        if (port > 65535)
            return EAI_SERVICE;
    }
    struct in_addr ip;
    if (!node)
        ip.s_addr = flags & AI_PASSIVE ? INADDR_ANY : htonl(INADDR_LOOPBACK);
    else if (inet_pton(AF_INET, node, &ip) != 1) {
        if (flags & AI_NUMERICHOST)
            return EAI_NONAME;
        if (netresolve(node, &ip) < 0)
            return errno == EAGAIN ? EAI_AGAIN : EAI_NONAME;
    }
    struct addrinfo *r = calloc(1, sizeof(*r));
    if (!r)
        return EAI_MEMORY;
    struct sockaddr_in *address = calloc(1, sizeof(*address));
    if (!address) {
        free(r);
        return EAI_MEMORY;
    }
    address->sin_family = AF_INET;
    address->sin_port = htons(port);
    address->sin_addr = ip;
    r->ai_family = AF_INET;
    r->ai_socktype = type;
    r->ai_protocol = protocol;
    r->ai_flags = flags;
    r->ai_addrlen = sizeof(*address);
    r->ai_addr = (struct sockaddr *)address;
    if ((flags & AI_CANONNAME) && node) {
        r->ai_canonname = strdup(node);
        if (!r->ai_canonname) {
            freeaddrinfo(r);
            return EAI_MEMORY;
        }
    }
    *result = r;
    return 0;
}
void freeaddrinfo(struct addrinfo *result) {
    while (result) {
        struct addrinfo *next = result->ai_next;
        free(result->ai_addr);
        free(result->ai_canonname);
        free(result);
        result = next;
    }
}
const char *gai_strerror(int error) {
    switch (error) {
    case 0:
        return "Success";
    case EAI_BADFLAGS:
        return "Invalid resolver flags";
    case EAI_NONAME:
        return "Name or service not found";
    case EAI_AGAIN:
        return "Temporary name resolution failure";
    case EAI_FAMILY:
        return "Unsupported address family";
    case EAI_SOCKTYPE:
        return "Unsupported socket type";
    case EAI_SERVICE:
        return "Invalid service port";
    case EAI_MEMORY:
        return "Out of memory";
    case EAI_SYSTEM:
        return "Resolver system error";
    default:
        return "Name resolution failed";
    }
}
struct hostent *gethostbyname(const char *name) {
    static struct hostent host;
    static struct in_addr address;
    static char hostname[256];
    static char *addresses[2], *aliases[1];
    if (!name || strlen(name) >= sizeof(hostname)) {
        errno = EINVAL;
        return NULL;
    }
    if (netresolve(name, &address) < 0)
        return NULL;
    strcpy(hostname, name);
    addresses[0] = (char *)&address;
    addresses[1] = NULL;
    aliases[0] = NULL;
    host = (struct hostent){hostname, aliases, AF_INET, 4, addresses};
    return &host;
}
