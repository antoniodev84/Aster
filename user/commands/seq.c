#include "common.h"
int main(int argc, char **argv) {
    long first = 1, step = 1, last;
    if (argc == 2)
        last = strtol(argv[1], NULL, 10);
    else if (argc == 3) {
        first = strtol(argv[1], NULL, 10);
        last = strtol(argv[2], NULL, 10);
    } else if (argc == 4) {
        first = strtol(argv[1], NULL, 10);
        step = strtol(argv[2], NULL, 10);
        last = strtol(argv[3], NULL, 10);
    } else
        return 2;
    if (!step)
        return 2;
    for (long n = first; step > 0 ? n <= last : n >= last;) {
        printf("%ld\n", n);
        if ((step > 0 && n > LONG_MAX - step) || (step < 0 && n < LONG_MIN - step))
            break;
        n += step;
    }
    return ferror(stdout) ? 1 : 0;
}
