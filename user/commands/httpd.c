#include "httpcommon.h"
static void reap(int sig) {
    (void)sig;
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) {
    }
}
static int hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static int open_resource(int root, char *url) {
    if (*url != '/') {
        errno = EACCES;
        return -1;
    }
    char path[4096];
    size_t used = 0;
    for (size_t i = 1; url[i] && url[i] != '?' && url[i] != '#'; i++) {
        unsigned char c = url[i];
        if (c == '%') {
            if (!url[i + 1] || !url[i + 2] || hex(url[i + 1]) < 0 || hex(url[i + 2]) < 0) {
                errno = EINVAL;
                return -1;
            }
            c = (hex(url[i + 1]) << 4) | hex(url[i + 2]);
            i += 2;
        }
        if (c < 32 || c == 127 || c == '\\') {
            errno = EACCES;
            return -1;
        }
        if (used + 12 >= sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        path[used++] = c;
    }
    if (!used || path[used - 1] == '/') {
        memcpy(path + used, "index.html", 10);
        used += 10;
    }
    path[used] = 0;
    int directory = dup(root);
    if (directory < 0)
        return -1;
    char *component = path;
    for (;;) {
        while (*component == '/')
            component++;
        char *slash = strchr(component, '/');
        if (slash)
            *slash = 0;
        if (!*component || !strcmp(component, ".") || !strcmp(component, "..")) {
            close(directory);
            errno = EACCES;
            return -1;
        }
        int fd = openat(directory, component, O_RDONLY | O_NOFOLLOW | (slash ? O_DIRECTORY : 0));
        int saved = errno;
        close(directory);
        errno = saved;
        if (fd < 0)
            return -1;
        if (!slash)
            return fd;
        directory = fd;
        component = slash + 1;
    }
}
static void response(int fd, int status, const char *reason, bool head) {
    char header[512], body[128];
    int n = snprintf(body, sizeof(body), "%d %s\n", status, reason);
    int length = snprintf(header, sizeof(header),
                          "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nContent-Length: "
                          "%d\r\nConnection: close\r\n\r\n",
                          status, reason, n);
    if (net_send_all(fd, header, length) == 0 && !head)
        net_send_all(fd, body, n);
}
static void serve(int client, int root) {
    bool head = false;
    if (net_timeout(client, 10000) < 0)
        return;
    struct http_reader reader = {.fd = client};
    char line[8192];
    if (http_line(&reader, line, sizeof(line)) < 0) {
        response(client, 400, "Bad Request", head);
        return;
    }
    char *url = strchr(line, ' ');
    if (!url) {
        response(client, 400, "Bad Request", head);
        return;
    }
    *url++ = 0;
    head = !strcmp(line, "HEAD");
    if (strcmp(line, "GET") && !head) {
        response(client, 405, "Method Not Allowed", head);
        return;
    }
    char *version = strchr(url, ' ');
    if (!version) {
        response(client, 400, "Bad Request", head);
        return;
    }
    *version++ = 0;
    if (strcmp(version, "HTTP/1.0") && strcmp(version, "HTTP/1.1")) {
        response(client, 505, "HTTP Version Not Supported", head);
        return;
    }
    bool require_host = !strcmp(version, "HTTP/1.1"), host = false;
    char resource[4096];
    if (strlen(url) >= sizeof(resource)) {
        response(client, 414, "URI Too Long", head);
        return;
    }
    strcpy(resource, url);
    size_t total = 0;
    for (;;) {
        int n = http_line(&reader, line, sizeof(line));
        if (n < 0) {
            response(client, 400, "Bad Request", head);
            return;
        }
        if (!n)
            break;
        total += n + 2;
        if (total > 32768) {
            response(client, 431, "Request Header Fields Too Large", head);
            return;
        }
        char *value = http_header(line);
        if (!value) {
            response(client, 400, "Bad Request", head);
            return;
        }
        if (http_equal(line, "Host")) {
            if (host || !*value) {
                response(client, 400, "Bad Request", head);
                return;
            }
            host = true;
        }
    }
    if (require_host && !host) {
        response(client, 400, "Bad Request", head);
        return;
    }
    int file = open_resource(root, resource);
    if (file < 0) {
        int code = errno == ENOENT || errno == ENOTDIR ? 404
                   : errno == EACCES || errno == ELOOP ? 403
                   : errno == EINVAL                   ? 400
                                                       : 500;
        response(client, code,
                 code == 404   ? "Not Found"
                 : code == 403 ? "Forbidden"
                 : code == 400 ? "Bad Request"
                               : "Internal Server Error",
                 head);
        return;
    }
    struct stat metadata;
    if (fstat(file, &metadata) < 0 || !S_ISREG(metadata.st_mode)) {
        close(file);
        response(client, 403, "Forbidden", head);
        return;
    }
    const char *mime = "application/octet-stream";
    char *ext = strrchr(resource, '.');
    if (ext && (!strcmp(ext, ".html") || !strcmp(ext, ".htm")))
        mime = "text/html; charset=utf-8";
    else if (ext && !strcmp(ext, ".txt"))
        mime = "text/plain; charset=utf-8";
    else if (ext && !strcmp(ext, ".css"))
        mime = "text/css";
    else if (ext && !strcmp(ext, ".js"))
        mime = "application/javascript";
    int n = snprintf(
        line, sizeof(line),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\nConnection: close\r\n\r\n",
        mime, (long long)metadata.st_size);
    if (net_send_all(client, line, n) == 0 && !head) {
        uint64_t left = metadata.st_size;
        while (left) {
            size_t count = left < sizeof(line) ? left : sizeof(line);
            ssize_t length = read(file, line, count);
            if (length <= 0 || net_send_all(client, line, length) < 0)
                break;
            left -= length;
        }
    }
    close(file);
}
int main(int argc, char **argv) {
    unsigned port = 8080;
    const char *directory = ".";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            if (net_number(argv[++i], 1, 65535, &port) < 0)
                goto usage;
        } else if (!strcmp(argv[i], "-h") && i + 1 < argc)
            directory = argv[++i];
        else
            goto usage;
    }
    int root = open(directory, O_RDONLY | O_DIRECTORY);
    if (root < 0) {
        perror("httpd: root");
        return 1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("httpd: socket");
        close(root);
        return 1;
    }
    int yes = 1;
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(port)};
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0 ||
        bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(fd, 16) < 0) {
        perror("httpd");
        close(fd);
        close(root);
        return 1;
    }
    signal(SIGCHLD, reap);
    signal(SIGPIPE, SIG_IGN);
    printf("httpd: serving %s on 0.0.0.0:%u\n", directory, port);
    for (;;) {
        int client = accept(fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            perror("httpd: accept");
            break;
        }
        pid_t pid = fork();
        if (!pid) {
            close(fd);
            serve(client, root);
            close(client);
            close(root);
            _exit(0);
        }
        if (pid < 0)
            perror("httpd: fork");
        close(client);
    }
    close(root);
    close(fd);
    return 1;
usage:
    fputs("Usage: httpd [-p PORT] [-h DOCUMENT_ROOT]\n", stderr);
    return 2;
}
