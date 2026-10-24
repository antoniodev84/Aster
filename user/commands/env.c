#include "common.h"
int main(int argc, char **argv) {
    int i = 1;
    if (i < argc && !strcmp(argv[i], "-i")) {
        clearenv();
        i++;
    }
    for (; i < argc && strchr(argv[i], '='); i++)
        if (putenv(argv[i]) < 0) {
            perror("env");
            return 1;
        }
    if (i < argc) {
        execvp(argv[i], argv + i);
        perror(argv[i]);
        return errno == ENOENT ? 127 : 126;
    }
    for (char **p = environ; p && *p; p++)
        puts(*p);
    return 0;
}
