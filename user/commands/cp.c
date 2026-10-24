#include "common.h"
int main(int argc, char **argv) {
    if (argc != 3) {
        fputs("usage: cp source destination\n", stderr);
        return 2;
    }
    int in = open(argv[1], O_RDONLY);
    if (in < 0) {
        perror(argv[1]);
        return 1;
    }
    struct stat source, target;
    if (fstat(in, &source) < 0) {
        perror(argv[1]);
        close(in);
        return 1;
    }
    if (stat(argv[2], &target) == 0 && source.st_dev == target.st_dev &&
        source.st_ino == target.st_ino) {
        fputs("cp: source and destination are the same file\n", stderr);
        close(in);
        return 1;
    }
    int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, source.st_mode & 0777);
    if (out < 0) {
        perror(argv[2]);
        close(in);
        return 1;
    }
    int result = copy_fd(in, out);
    if (result < 0)
        perror("cp");
    if (fsync(out) < 0) {
        perror("cp: fsync");
        result = -1;
    }
    close(in);
    close(out);
    return result < 0;
}
