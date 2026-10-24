#ifndef ASTER_SYS_SOCKET_H
#define ASTER_SYS_SOCKET_H
#include <os.h>
#define AF_UNSPEC 0
#define PF_INET AF_INET
typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;
struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};
int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr *address, socklen_t length);
int connect(int fd, const struct sockaddr *address, socklen_t length);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *address, socklen_t *length);
ssize_t send(int fd, const void *buffer, size_t size, int flags);
ssize_t recv(int fd, void *buffer, size_t size, int flags);
ssize_t sendto(int fd, const void *buffer, size_t size, int flags, const struct sockaddr *address,
               socklen_t length);
ssize_t recvfrom(int fd, void *buffer, size_t size, int flags, struct sockaddr *address,
                 socklen_t *length);
int shutdown(int fd, int how);
int getsockname(int fd, struct sockaddr *address, socklen_t *length);
int getpeername(int fd, struct sockaddr *address, socklen_t *length);
int setsockopt(int fd, int level, int option, const void *value, socklen_t length);
int getsockopt(int fd, int level, int option, void *value, socklen_t *length);
#endif
