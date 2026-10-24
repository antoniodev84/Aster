#include "netcommon.h"
int main(int argc, char **argv) {
    const char *interface = NULL;
    unsigned timeout = 30;
    bool release = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-i") && i + 1 < argc)
            interface = argv[++i];
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            if (net_number(argv[++i], 1, 86400, &timeout) < 0)
                goto usage;
        } else if (!strcmp(argv[i], "-r"))
            release = true;
        else
            goto usage;
    }
    struct abi_netconfig config;
    if (net_interface(interface, &config) < 0) {
        perror("udhcp");
        return 1;
    }
    if (release) {
        if (netdhcp(config.index, 2) < 0 || netdhcp(config.index, 3) < 0) {
            perror("udhcp");
            return 1;
        }
        printf("%s: lease released\n", config.name);
        return 0;
    }
    if (netdhcp(config.index, 0) < 0) {
        perror("udhcp");
        return 1;
    }
    uint64_t deadline = net_milliseconds() + timeout * 1000ull;
    for (;;) {
        int state = netdhcp(config.index, 1);
        if (state < 0) {
            perror("udhcp");
            return 1;
        }
        if (state) {
            if (netconfig(&config, 0) < 0) {
                perror("udhcp");
                return 1;
            }
            net_print_config(&config);
            return 0;
        }
        if (net_milliseconds() >= deadline) {
            fputs("udhcp: lease acquisition timed out\n", stderr);
            return 1;
        }
        struct timespec delay = {0, 100000000};
        if (nanosleep(&delay, NULL) < 0 && errno != EINTR) {
            perror("udhcp");
            return 1;
        }
    }
usage:
    fputs("Usage: udhcp [-i INTERFACE] [-t TIMEOUT_SECONDS] [-r]\n", stderr);
    return 2;
}
