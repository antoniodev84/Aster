#include "common.h"
static int make_path(char *path, mode_t mode) {
    for (char *p = path + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            if (mkdir(path, mode) < 0 && errno != EEXIST) {
                *p = '/';
                return -1;
            }
            *p = '/';
        }
    if (mkdir(path, mode) < 0) {
        if (errno != EEXIST)
            return -1;
        struct stat s;
        if (stat(path, &s) < 0 || !S_ISDIR(s.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
    }
    return 0;
}
int main(int argc, char **argv) {
    bool parents = false;
    mode_t mode = 0777;
    int i = 1, status = 0;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-p"))
            parents = true;
        else if (!strcmp(argv[i], "-m") && i + 1 < argc)
            mode = strtoul(argv[++i], NULL, 8);
        else
            break;
    }
    if (i == argc) {
        fputs("usage: mkdir [-p] [-m mode] path...\n", stderr);
        return 2;
    }
    for (; i < argc; i++)
        if ((parents ? make_path(argv[i], mode) : mkdir(argv[i], mode)) < 0) {
            perror(argv[i]);
            status = 1;
        }
    return status;
}
