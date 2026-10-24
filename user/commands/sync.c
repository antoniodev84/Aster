#include "common.h"
int main(int argc, char **argv) {
    int status = 0;
    if (argc == 1) {
        int fd = open("/", O_RDONLY | O_DIRECTORY);
        if (fd < 0 || fsync(fd) < 0) {
            perror("sync");
            status = 1;
        }
        if (fd >= 0)
            close(fd);
    } else
        for (int i = 1; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0 || fsync(fd) < 0) {
                perror(argv[i]);
                status = 1;
            }
            if (fd >= 0)
                close(fd);
        }
    return status;
}
