#include "common.h"
int main(int argc, char **argv) {
    bool symbolic = false, force = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        for (char *p = argv[i] + 1; *p; p++)
            if (*p == 's')
                symbolic = true;
            else if (*p == 'f')
                force = true;
            else
                return 2;
    }
    if (argc - i != 2) {
        fputs("usage: ln [-s] [-f] target link\n", stderr);
        return 2;
    }
    if (force)
        unlink(argv[i + 1]);
    if ((symbolic ? symlink(argv[i], argv[i + 1]) : link(argv[i], argv[i + 1])) < 0) {
        perror("ln");
        return 1;
    }
    return 0;
}
