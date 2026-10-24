#include "common.h"
int main(int argc, char **argv) {
    if (argc < 3) {
        fputs("usage: chmod octal-mode path...\n", stderr);
        return 2;
    }
    char *end;
    mode_t mode = strtoul(argv[1], &end, 8);
    if (*end || mode > 07777) {
        fputs("chmod: invalid mode\n", stderr);
        return 2;
    }
    int status = 0;
    for (int i = 2; i < argc; i++)
        if (chmod(argv[i], mode) < 0) {
            perror(argv[i]);
            status = 1;
        }
    return status;
}
