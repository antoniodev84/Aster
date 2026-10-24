#include "common.h"
int main(int argc, char **argv) {
    int result = 0;
    if (argc == 1) {
        if (copy_fd(0, 1) < 0) {
            perror("cat");
            return 1;
        }
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        int fd = !strcmp(argv[i], "-") ? 0 : open(argv[i], O_RDONLY);
        if (fd < 0) {
            perror(argv[i]);
            result = 1;
            continue;
        }
        if (copy_fd(fd, 1) < 0) {
            perror(argv[i]);
            result = 1;
        }
        if (fd)
            close(fd);
    }
    return result;
}
