#include "common.h"
int main(int argc, char **argv) {
    int fds[128], used = 0, status = 0, i = 1;
    bool append = false;
    if (i < argc && !strcmp(argv[i], "-a")) {
        append = true;
        i++;
    }
    for (; i < argc && used < 128; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0666);
        if (fd < 0) {
            perror(argv[i]);
            status = 1;
        } else
            fds[used++] = fd;
    }
    char b[4096];
    ssize_t n;
    while ((n = read(0, b, sizeof(b))) > 0) {
        if (output_all(1, b, n) < 0)
            status = 1;
        for (int j = 0; j < used; j++)
            if (output_all(fds[j], b, n) < 0) {
                perror("tee");
                status = 1;
            }
    }
    if (n < 0) {
        perror("tee");
        status = 1;
    }
    for (int j = 0; j < used; j++) {
        if (fsync(fds[j]) < 0)
            status = 1;
        close(fds[j]);
    }
    return status;
}
