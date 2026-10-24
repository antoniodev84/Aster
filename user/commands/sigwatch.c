#include "common.h"
static volatile unsigned received;
static void handler(int signal) {
    (void)signal;
    received++;
    write(1, "SIGUSR1 received\n", 17);
}
int main(int argc, char **argv) {
    unsigned count = argc > 1 ? strtoul(argv[1], NULL, 10) : 1;
    struct sigaction action = {.sa_handler = handler};
    if (sigaction(SIGUSR1, &action, NULL) < 0) {
        perror("sigwatch");
        return 1;
    }
    printf("sigwatch pid=%d target=%u\n", getpid(), count);
    fflush(stdout);
    while (received < count)
        pause();
    return 0;
}
