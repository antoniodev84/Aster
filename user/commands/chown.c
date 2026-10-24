#include "common.h"
int main(int argc, char **argv) {
    if (argc < 3) {
        fputs("usage: chown uid[:gid] path...\n", stderr);
        return 2;
    }
    char *colon = strchr(argv[1], ':');
    gid_t gid = -1;
    if (colon) {
        *colon = 0;
        gid = !strcmp(colon + 1, "root") ? 0 : strtoul(colon + 1, NULL, 10);
    }
    uid_t uid = !strcmp(argv[1], "root") ? 0 : *argv[1] ? strtoul(argv[1], NULL, 10) : (uid_t)-1;
    int result = 0;
    for (int i = 2; i < argc; i++)
        if (chown(argv[i], uid, gid) < 0) {
            perror(argv[i]);
            result = 1;
        }
    return result;
}
