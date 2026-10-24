#include "common.h"
int main(int argc, char **argv) {
    int fd = argc > 1 ? open(argv[1], O_RDONLY) : 0;
    if (fd < 0) {
        perror(argv[1]);
        return 1;
    }
    unsigned char b[16];
    unsigned long offset = 0;
    ssize_t n;
    while ((n = read(fd, b, 16)) > 0) {
        printf("%08lx  ", offset);
        for (int i = 0; i < 16; i++)
            if (i < n)
                printf("%02x ", b[i]);
            else
                printf("   ");
        printf(" |");
        for (int i = 0; i < n; i++)
            putchar(isprint(b[i]) ? b[i] : '.');
        puts("|");
        offset += n;
    }
    if (fd)
        close(fd);
    return n < 0;
}
