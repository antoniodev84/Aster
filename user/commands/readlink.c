#include "common.h"
int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    bool canonical = !strcmp(argv[1], "-f");
    int result = 0;
    for (int i = canonical ? 2 : 1; i < argc; i++) {
        char b[4096];
        if (canonical) {
            if (!realpath(argv[i], b)) {
                perror(argv[i]);
                result = 1;
                continue;
            }
        } else {
            ssize_t n = readlink(argv[i], b, sizeof(b) - 1);
            if (n < 0) {
                perror(argv[i]);
                result = 1;
                continue;
            }
            b[n] = 0;
        }
        puts(b);
    }
    return result;
}
