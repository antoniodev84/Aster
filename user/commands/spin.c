#include "common.h"
int main(int argc, char **argv) {
    unsigned long n = argc > 1 ? strtoul(argv[1], NULL, 10) : 1000000000;
    volatile uint64_t state = 0x123456789abcdefull;
    for (unsigned long i = 0; i < n; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
    }
    printf("pid=%d iterations=%lu checksum=%016lx\n", getpid(), n, (unsigned long)state);
    return 0;
}
