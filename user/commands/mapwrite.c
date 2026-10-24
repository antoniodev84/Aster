#include "common.h"
int main(int argc, char **argv) {
    if (argc != 4) {
        fputs("usage: mapwrite FILE OFFSET TEXT\n", stderr);
        return 2;
    }
    char *end;
    unsigned long offset = strtoul(argv[2], &end, 0);
    size_t count = strlen(argv[3]);
    if (*end || offset > SIZE_MAX - count) {
        fputs("mapwrite: invalid offset\n", stderr);
        return 2;
    }
    int fd = open(argv[1], O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        perror(argv[1]);
        return 1;
    }
    struct stat s;
    if (fstat(fd, &s) < 0) {
        perror("mapwrite: fstat");
        close(fd);
        return 1;
    }
    size_t length = s.st_size;
    if (length < offset + count) {
        length = offset + count;
        if (ftruncate(fd, length) < 0) {
            perror("mapwrite: ftruncate");
            close(fd);
            return 1;
        }
    }
    if (!length) {
        close(fd);
        return 0;
    }
    char *mapped = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mapwrite: mmap");
        close(fd);
        return 1;
    }
    memcpy(mapped + offset, argv[3], count);
    int status = 0;
    if (munmap(mapped, length) < 0 || fsync(fd) < 0) {
        perror("mapwrite: sync");
        status = 1;
    }
    if (close(fd) < 0)
        status = 1;
    return status;
}
