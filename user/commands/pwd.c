#include "common.h"
int main(void) {
    char *path = getcwd(NULL, 0);
    if (!path) {
        perror("pwd");
        return 1;
    }
    puts(path);
    free(path);
    return 0;
}
