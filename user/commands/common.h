#ifndef COMMAND_COMMON_H
#define COMMAND_COMMON_H
#include <os.h>
#include <limits.h>
static inline int output_all(int fd, const void *buffer, size_t count) {
    const char *p = buffer;
    while (count) {
        ssize_t n = write(fd, p, count);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (!n) {
            errno = EIO;
            return -1;
        }
        p += n;
        count -= n;
    }
    return 0;
}
static inline int copy_fd(int source, int target) {
    char buffer[16384];
    ssize_t n;
    while ((n = read(source, buffer, sizeof(buffer))) > 0)
        if (output_all(target, buffer, n) < 0)
            return -1;
    return n < 0 ? -1 : 0;
}
static inline char *load_file(const char *path, size_t *length) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    size_t used = 0, capacity = 4096;
    char *data = malloc(capacity);
    if (!data) {
        fclose(f);
        return NULL;
    }
    for (;;) {
        if (used + 1024 + 1 > capacity) {
            capacity *= 2;
            char *p = realloc(data, capacity);
            if (!p) {
                free(data);
                fclose(f);
                return NULL;
            }
            data = p;
        }
        size_t n = fread(data + used, 1, capacity - used - 1, f);
        used += n;
        if (!n)
            break;
    }
    if (ferror(f)) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[used] = 0;
    *length = used;
    return data;
}
static inline const char *type_name(mode_t mode) {
    return S_ISDIR(mode)                ? "directory"
           : S_ISLNK(mode)              ? "symbolic link"
           : S_ISCHR(mode)              ? "character device"
           : S_ISREG(mode)              ? "regular file"
           : (mode & S_IFMT) == S_IFBLK ? "block device"
                                        : "other";
}
#endif
