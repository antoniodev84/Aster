#include "httpcommon.h"
struct url {
    char host[256], path[4096];
    unsigned port;
};
static int parse_url(const char *text, struct url *url) {
    if (strncmp(text, "http://", 7)) {
        errno = ENOTSUP;
        return -1;
    }
    text += 7;
    const char *end = text;
    while (*end && *end != '/' && *end != '?' && *end != '#')
        end++;
    const char *colon = NULL;
    for (const char *p = text; p < end; p++) {
        if (*p == ':') {
            if (colon) {
                errno = EINVAL;
                return -1;
            }
            colon = p;
        } else if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                     (*p >= '0' && *p <= '9') || *p == '.' || *p == '-')) {
            errno = EINVAL;
            return -1;
        }
    }
    size_t length = (colon ? colon : end) - text;
    if (!length || length >= sizeof(url->host)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(url->host, text, length);
    url->host[length] = 0;
    url->port = 80;
    if (colon) {
        char service[8];
        length = end - colon - 1;
        if (!length || length >= sizeof(service)) {
            errno = EINVAL;
            return -1;
        }
        memcpy(service, colon + 1, length);
        service[length] = 0;
        if (net_number(service, 1, 65535, &url->port) < 0) {
            errno = EINVAL;
            return -1;
        }
    }
    size_t used = 0;
    if (*end != '/')
        url->path[used++] = '/';
    while (*end && *end != '#') {
        if ((unsigned char)*end <= 32 || (unsigned char)*end == 127 ||
            used + 1 >= sizeof(url->path)) {
            errno = EINVAL;
            return -1;
        }
        url->path[used++] = *end++;
    }
    url->path[used] = 0;
    return 0;
}
static int redirect_url(struct url *url, const char *location) {
    if (!*location) {
        errno = EINVAL;
        return -1;
    }
    if (!strncmp(location, "http://", 7) || !strncmp(location, "https://", 8))
        return parse_url(location, url);
    char text[4608];
    if (location[0] == '/' && location[1] == '/') {
        if (snprintf(text, sizeof(text), "http:%s", location) >= (int)sizeof(text)) {
            errno = EOVERFLOW;
            return -1;
        }
        return parse_url(text, url);
    }
    char path[4096];
    if (location[0] == '/') {
        if (strlen(location) >= sizeof(path)) {
            errno = EOVERFLOW;
            return -1;
        }
        strcpy(path, location);
    } else {
        strcpy(path, url->path);
        char *query = strchr(path, '?');
        if (query)
            *query = 0;
        if (location[0] != '?' && location[0] != '#') {
            char *slash = strrchr(path, '/');
            slash[1] = 0;
        }
        if (strlen(path) + strlen(location) >= sizeof(path)) {
            errno = EOVERFLOW;
            return -1;
        }
        strcat(path, location);
    }
    char normalized[4096];
    size_t used = 0;
    const char *p = path;
    while (*p && *p != '?' && *p != '#') {
        if (*p == '/') {
            if (!used || normalized[used - 1] != '/')
                normalized[used++] = '/';
            p++;
            continue;
        }
        const char *end = p;
        while (*end && *end != '/' && *end != '?' && *end != '#')
            end++;
        size_t size = end - p;
        if (size == 1 && *p == '.') {
        } else if (size == 2 && p[0] == '.' && p[1] == '.') {
            if (used > 1)
                used--;
            while (used > 1 && normalized[used - 1] != '/')
                used--;
        } else {
            memcpy(normalized + used, p, size);
            used += size;
        }
        p = end;
    }
    if (!used)
        normalized[used++] = '/';
    if (used + strlen(p) >= sizeof(normalized)) {
        errno = EOVERFLOW;
        return -1;
    }
    strcpy(normalized + used, p);
    int n = snprintf(text, sizeof(text), "http://%s:%u%s", url->host, url->port, normalized);
    if (n < 0 || n >= (int)sizeof(text)) {
        errno = EOVERFLOW;
        return -1;
    }
    return parse_url(text, url);
}
static int connect_url(const struct url *url) {
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(url->port)};
    if (netresolve(url->host, &peer.sin_addr) < 0)
        return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    if (net_timeout(fd, 30000) < 0 || connect(fd, (struct sockaddr *)&peer, sizeof(peer)) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    char request[4608];
    int length = snprintf(request, sizeof(request),
                          "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUser-Agent: Aster-wget/1.0\r\nAccept: "
                          "*/*\r\nConnection: close\r\n\r\n",
                          url->path, url->host, url->port);
    if (length < 0 || length >= (int)sizeof(request) || net_send_all(fd, request, length) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}
static int transfer(struct http_reader *reader, int output, uint64_t length, bool bounded,
                    uint64_t *total) {
    char buffer[8192];
    while (length || !bounded) {
        size_t size = bounded && length < sizeof(buffer) ? length : sizeof(buffer);
        ssize_t n = http_read(reader, buffer, size);
        if (n < 0)
            return -1;
        if (!n) {
            if (bounded) {
                errno = EIO;
                return -1;
            }
            return 0;
        }
        if (output_all(output, buffer, n) < 0)
            return -1;
        *total += n;
        if (bounded)
            length -= n;
    }
    return 0;
}
static int chunks(struct http_reader *reader, int output, uint64_t *total) {
    char line[8192];
    for (;;) {
        if (http_line(reader, line, sizeof(line)) < 0)
            return -1;
        uint64_t size = 0;
        const char *p = line;
        unsigned digits = 0;
        while (*p && *p != ';') {
            unsigned value = *p >= '0' && *p <= '9'   ? (unsigned)(*p - '0')
                             : *p >= 'a' && *p <= 'f' ? (unsigned)(*p - 'a' + 10)
                             : *p >= 'A' && *p <= 'F' ? (unsigned)(*p - 'A' + 10)
                                                      : 16;
            if (value == 16 || size > (UINT64_MAX - value) / 16) {
                errno = EIO;
                return -1;
            }
            size = size * 16 + value;
            digits++;
            p++;
        }
        if (!digits) {
            errno = EIO;
            return -1;
        }
        if (!size) {
            size_t total_headers = 0;
            for (;;) {
                int n = http_line(reader, line, sizeof(line));
                if (n < 0)
                    return -1;
                if (!n)
                    return 0;
                total_headers += n + 2;
                if (total_headers > 32768 || !http_header(line)) {
                    errno = EIO;
                    return -1;
                }
            }
        }
        if (transfer(reader, output, size, true, total) < 0)
            return -1;
        char crlf[2];
        if (http_exact(reader, crlf, 2) < 0 || crlf[0] != '\r' || crlf[1] != '\n') {
            errno = EIO;
            return -1;
        }
    }
}
int main(int argc, char **argv) {
    const char *destination = NULL, *input = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-O") && i + 1 < argc)
            destination = argv[++i];
        else if (argv[i][0] == '-' || input)
            goto usage;
        else
            input = argv[i];
    }
    if (!input)
        goto usage;
    struct url url;
    if (parse_url(input, &url) < 0) {
        perror("wget: URL (only HTTP is supported)");
        return 1;
    }
    for (unsigned redirects = 0; redirects <= 10; redirects++) {
        int fd = connect_url(&url);
        if (fd < 0) {
            perror("wget: connect");
            return 1;
        }
        struct http_reader reader = {.fd = fd};
        char line[8192], location[4096] = {0};
        bool chunked = false, has_length = false;
        uint64_t content_length = 0;
        unsigned status = 0, interim = 0;
        for (;;) {
            int n = http_line(&reader, line, sizeof(line));
            if (n < 12 || (strncmp(line, "HTTP/1.1 ", 9) && strncmp(line, "HTTP/1.0 ", 9)) ||
                line[9] < '1' || line[9] > '5' || line[10] < '0' || line[10] > '9' ||
                line[11] < '0' || line[11] > '9' || (line[12] && line[12] != ' '))
                goto protocol_error;
            status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
            size_t total_headers = n + 2;
            for (;;) {
                n = http_line(&reader, line, sizeof(line));
                if (n < 0)
                    goto protocol_error;
                if (!n)
                    break;
                total_headers += n + 2;
                if (total_headers > 65536)
                    goto protocol_error;
                char *value = http_header(line);
                if (!value)
                    goto protocol_error;
                if (http_equal(line, "Content-Length")) {
                    uint64_t length;
                    if (http_decimal(value, &length) < 0 ||
                        (has_length && length != content_length))
                        goto protocol_error;
                    content_length = length;
                    has_length = true;
                } else if (http_equal(line, "Transfer-Encoding")) {
                    if (chunked || !http_equal(value, "chunked"))
                        goto protocol_error;
                    chunked = true;
                } else if (http_equal(line, "Location")) {
                    if (strlen(value) >= sizeof(location))
                        goto protocol_error;
                    strcpy(location, value);
                }
            }
            if (status >= 200)
                break;
            if (status == 101 || ++interim > 8)
                goto protocol_error;
            chunked = has_length = false;
            location[0] = 0;
        }
        if (chunked && has_length)
            goto protocol_error;
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            close(fd);
            if (redirects == 10) {
                fputs("wget: too many redirects\n", stderr);
                return 1;
            }
            if (redirect_url(&url, location) < 0) {
                perror("wget: redirect");
                return 1;
            }
            continue;
        }
        if (status < 200 || status >= 300) {
            fprintf(stderr, "wget: HTTP status %u\n", status);
            close(fd);
            return 1;
        }
        char filename[4096];
        const char *output_path = destination;
        if (!output_path) {
            strcpy(filename, url.path);
            char *query = strchr(filename, '?');
            if (query)
                *query = 0;
            char *name = strrchr(filename, '/');
            output_path = name && name[1] ? name + 1 : "index.html";
            if (!strcmp(output_path, ".") || !strcmp(output_path, ".."))
                output_path = "index.html";
        }
        bool stdout_output = !strcmp(output_path, "-");
        int output =
            stdout_output ? STDOUT_FILENO : open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (output < 0) {
            perror("wget: output");
            close(fd);
            return 1;
        }
        uint64_t total = 0;
        int result = 0;
        if (status != 204 && status != 205)
            result = chunked ? chunks(&reader, output, &total)
                             : transfer(&reader, output, content_length, has_length, &total);
        if (result < 0)
            perror("wget: transfer");
        if (!stdout_output && close(output) < 0) {
            perror("wget: close");
            result = -1;
        }
        close(fd);
        if (!result && !stdout_output)
            fprintf(stderr, "Saved %s (%llu bytes)\n", output_path, (unsigned long long)total);
        return result < 0 ? 1 : 0;
    protocol_error:
        fputs("wget: invalid or incomplete HTTP response\n", stderr);
        close(fd);
        return 1;
    }
    return 1;
usage:
    fputs("Usage: wget [-O FILE|-] http://HOST[:PORT]/PATH\n", stderr);
    return 2;
}
