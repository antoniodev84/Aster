#include "common.h"
int main(void) {
    uid_t uid = geteuid();
    if (!uid)
        puts("root");
    else
        printf("%u\n", uid);
    return 0;
}
