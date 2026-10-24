#include "common.h"
int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    struct timespec before, after;
    clock_gettime(CLOCK_MONOTONIC, &before);
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        perror("time");
        return 1;
    }
    if (!pid) {
        execvp(argv[1], argv + 1);
        perror(argv[1]);
        _exit(127);
    }
    struct rusage usage;
    int status;
    while (wait4(pid, &status, 0, &usage) < 0)
        if (errno != EINTR) {
            perror("time");
            return 1;
        }
    clock_gettime(CLOCK_MONOTONIC, &after);
    long ms = (after.tv_sec - before.tv_sec) * 1000 + (after.tv_nsec - before.tv_nsec) / 1000000;
    fprintf(stderr,
            "real %ld.%03ld s\nuser %ld.%06ld s\nsys  %ld.%06ld s\nmaxrss %ld KiB, voluntary %ld, "
            "involuntary %ld\n",
            ms / 1000, ms % 1000, usage.ru_utime.tv_sec, usage.ru_utime.tv_usec,
            usage.ru_stime.tv_sec, usage.ru_stime.tv_usec, usage.ru_maxrss, usage.ru_nvcsw,
            usage.ru_nivcsw);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
