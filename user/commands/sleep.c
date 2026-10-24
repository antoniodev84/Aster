#include "common.h"
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    char *end;
    double seconds = strtod(argv[1], &end);
    if (*end || seconds < 0 || seconds > 86400000) {
        fputs("sleep: invalid duration\n", stderr);
        return 2;
    }
    struct timespec request = {(long)seconds, (long)((seconds - (long)seconds) * 1000000000.0)},
                    remaining;
    while (nanosleep(&request, &remaining) < 0) {
        if (errno != EINTR) {
            perror("sleep");
            return 1;
        }
        request = remaining;
    }
    return 0;
}
