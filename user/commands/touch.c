#include "common.h"
int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    int result = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0666);
        if (fd < 0) {
            perror(argv[i]);
            result = 1;
        } else
            close(fd);
    }
    return result;
}
