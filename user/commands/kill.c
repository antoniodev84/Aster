#include "common.h"
static int signal_number(const char *s) {
    if (!strncmp(s, "SIG", 3))
        s += 3;
    const char *names[] = {"HUP",  "INT",  "QUIT", "ILL",  "ABRT", "BUS",  "FPE",  "KILL", "USR1",
                           "SEGV", "USR2", "PIPE", "ALRM", "TERM", "CHLD", "CONT", "STOP", "TSTP"};
    int values[] = {1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        if (!strcmp(s, names[i]))
            return values[i];
    return atoi(s);
}
int main(int argc, char **argv) {
    int i = 1, signal = SIGTERM, result = 0;
    if (i < argc && !strcmp(argv[i], "-l")) {
        puts(
            "HUP INT QUIT ILL ABRT BUS FPE KILL USR1 SEGV USR2 PIPE ALRM TERM CHLD CONT STOP TSTP");
        return 0;
    }
    if (i < argc && argv[i][0] == '-' && argv[i][1])
        signal = signal_number(argv[i++] + 1);
    if (i == argc)
        return 2;
    for (; i < argc; i++)
        if (kill(atoi(argv[i]), signal) < 0) {
            perror(argv[i]);
            result = 1;
        }
    return result;
}
