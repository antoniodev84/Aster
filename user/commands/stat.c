#include "common.h"
int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    int result = 0;
    for (int i = 1; i < argc; i++) {
        struct stat s;
        if (lstat(argv[i], &s) < 0) {
            perror(argv[i]);
            result = 1;
            continue;
        }
        printf("File: %s\nSize: %ld  Blocks: %ld  Inode: %lu  Links: %lu\nType: %s  Mode: %04o  "
               "UID: %u  GID: %u\n",
               argv[i], s.st_size, s.st_blocks, s.st_ino, s.st_nlink, type_name(s.st_mode),
               s.st_mode & 07777, s.st_uid, s.st_gid);
    }
    return result;
}
