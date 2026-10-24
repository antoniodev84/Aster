#include "common.h"
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "-u"))
        printf("%u\n", geteuid());
    else if (argc > 1 && !strcmp(argv[1], "-g"))
        printf("%u\n", getegid());
    else
        printf("uid=%u euid=%u gid=%u egid=%u\n", getuid(), geteuid(), getgid(), getegid());
    return 0;
}
