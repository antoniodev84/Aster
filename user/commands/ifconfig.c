#include "netcommon.h"
int main(int argc, char **argv) {
    struct abi_netconfig config;
    if (argc == 1) {
        unsigned count = 0;
        for (unsigned i = 0; i < 4; i++) {
            config = (struct abi_netconfig){.index = i};
            if (netconfig(&config, 0) < 0) {
                if (errno != ENODEV) {
                    perror("ifconfig");
                    return 1;
                }
                break;
            }
            net_print_config(&config);
            count++;
        }
        if (!count)
            puts("No Ethernet interfaces");
        return 0;
    }
    if (net_interface(argv[1], &config) < 0) {
        perror("ifconfig");
        return 1;
    }
    if (argc == 2) {
        net_print_config(&config);
        return 0;
    }
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "up"))
            config.flags |= NETCFG_UP;
        else if (!strcmp(argv[i], "down"))
            config.flags &= ~NETCFG_UP;
        else if (!strcmp(argv[i], "default"))
            config.flags |= NETCFG_DEFAULT;
        else {
            uint32_t *field = &config.address;
            if (!strcmp(argv[i], "netmask"))
                field = &config.netmask;
            else if (!strcmp(argv[i], "gateway"))
                field = &config.gateway;
            else if (!strcmp(argv[i], "dns"))
                field = &config.dns;
            else if (!strcmp(argv[i], "inet"))
                field = &config.address;
            else {
                if (inet_pton(AF_INET, argv[i], field) != 1)
                    goto usage;
                config.flags |= NETCFG_UP;
                continue;
            }
            if (++i == argc || inet_pton(AF_INET, argv[i], field) != 1)
                goto usage;
        }
    }
    if (netconfig(&config, 1) < 0) {
        perror("ifconfig");
        return 1;
    }
    if (netconfig(&config, 0) < 0) {
        perror("ifconfig");
        return 1;
    }
    net_print_config(&config);
    return 0;
usage:
    fputs("Usage: ifconfig [INTERFACE [up|down|inet ADDRESS|netmask MASK|gateway ADDRESS|dns "
          "ADDRESS|default]...]\n",
          stderr);
    return 2;
}
