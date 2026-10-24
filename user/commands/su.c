#include "common.h"
int main(int argc, char **argv) {
    if (geteuid() != 0) {
        fputs("su: root privileges required\n", stderr);
        return 1;
    }
    uid_t uid = argc > 1 ? strtoul(argv[1], NULL, 10) : 0;
    if (setuid(uid) < 0) {
        perror("su: setuid");
        return 1;
    }
    if (argc > 2)
        execvp(argv[2], argv + 2);
    else
        execl("/bin/sh", "sh", (char *)NULL);
    perror("su: exec");
    return 1;
}
