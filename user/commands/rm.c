#include "common.h"
static bool recursive, force;
static int erase(const char *path) {
    struct stat s;
    if (lstat(path, &s) < 0) {
        if (force && errno == ENOENT)
            return 0;
        perror(path);
        return 1;
    }
    if (S_ISDIR(s.st_mode)) {
        if (!recursive) {
            fprintf(stderr, "rm: %s: is a directory\n", path);
            return 1;
        }
        char resolved[4096];
        if (realpath(path, resolved) && !strcmp(resolved, "/")) {
            fputs("rm: refusing to remove root\n", stderr);
            return 1;
        }
        DIR *d = opendir(path);
        if (!d) {
            perror(path);
            return 1;
        }
        int result = 0;
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            char child[4096];
            if (snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= 4096) {
                result = 1;
                continue;
            }
            result |= erase(child);
        }
        closedir(d);
        if (!result && rmdir(path) < 0) {
            perror(path);
            result = 1;
        }
        return result;
    }
    if (unlink(path) < 0) {
        perror(path);
        return 1;
    }
    return 0;
}
int main(int argc, char **argv) {
    int i = 1, result = 0;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "--")) {
            i++;
            break;
        }
        for (char *p = argv[i] + 1; *p; p++) {
            if (*p == 'r' || *p == 'R')
                recursive = true;
            else if (*p == 'f')
                force = true;
            else
                return 2;
        }
    }
    if (i == argc)
        return force ? 0 : 2;
    for (; i < argc; i++)
        result |= erase(argv[i]);
    return result;
}
