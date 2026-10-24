#include <os.h>
struct DIR {
    int fd;
    size_t position, available;
    long offset;
    unsigned char buffer[4096];
};
DIR *fdopendir(int fd) {
    struct stat st;
    if (fstat(fd, &st) < 0)
        return NULL;
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }
    DIR *d = calloc(1, sizeof(*d));
    if (d)
        d->fd = fd;
    return d;
}
DIR *opendir(const char *path) {
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return NULL;
    DIR *d = fdopendir(fd);
    if (!d)
        close(fd);
    return d;
}
struct dirent *readdir(DIR *d) {
    if (d->position == d->available) {
        ssize_t n = getdents64(d->fd, d->buffer, sizeof(d->buffer));
        if (n <= 0)
            return NULL;
        d->position = 0;
        d->available = n;
    }
    struct dirent *entry = (void *)(d->buffer + d->position);
    if (entry->d_reclen < offsetof(struct dirent, d_name) + 1 ||
        entry->d_reclen > d->available - d->position) {
        errno = EIO;
        return NULL;
    }
    d->position += entry->d_reclen;
    d->offset = entry->d_off;
    return entry;
}
int closedir(DIR *d) {
    int result = close(d->fd);
    free(d);
    return result;
}
void rewinddir(DIR *d) {
    seekdir(d, 0);
}
int dirfd(DIR *d) {
    return d->fd;
}
long telldir(DIR *d) {
    return d->offset;
}
void seekdir(DIR *d, long offset) {
    if (lseek(d->fd, offset, SEEK_SET) >= 0) {
        d->position = d->available = 0;
        d->offset = offset;
    }
}
char *basename(char *path) {
    static char dot[] = ".", root[] = "/";
    if (!path || !*path)
        return dot;
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        path[--n] = 0;
    if (n == 1 && path[0] == '/')
        return root;
    char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}
char *dirname(char *path) {
    static char dot[] = ".", root[] = "/";
    if (!path || !*path)
        return dot;
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        path[--n] = 0;
    char *slash = strrchr(path, '/');
    if (!slash)
        return dot;
    while (slash > path && slash[-1] == '/')
        slash--;
    if (slash == path)
        return root;
    *slash = 0;
    return path;
}
char *realpath(const char *path, char *resolved) {
    if (!path || !*path) {
        errno = ENOENT;
        return NULL;
    }
    char *work = malloc(4096), *next = malloc(4096), *result = resolved ? resolved : malloc(4096);
    if (!work || !next || !result) {
        free(work);
        free(next);
        if (!resolved)
            free(result);
        return NULL;
    }
    if (strlen(path) >= 4096) {
        errno = ENAMETOOLONG;
        goto fail;
    }
    strcpy(work, path);
    if (path[0] == '/')
        strcpy(result, "/");
    else if (!getcwd(result, 4096))
        goto fail;
    unsigned links = 0;
    size_t at = 0;
    while (work[at]) {
        while (work[at] == '/')
            at++;
        if (!work[at])
            break;
        char part[256];
        size_t n = 0;
        while (work[at] && work[at] != '/') {
            if (n == 255) {
                errno = ENAMETOOLONG;
                goto fail;
            }
            part[n++] = work[at++];
        }
        part[n] = 0;
        if (!strcmp(part, "."))
            continue;
        if (!strcmp(part, "..")) {
            char *slash = strrchr(result, '/');
            if (slash == result)
                result[1] = 0;
            else if (slash)
                *slash = 0;
            continue;
        }
        size_t old = strlen(result);
        if (old + n + 2 >= 4096) {
            errno = ENAMETOOLONG;
            goto fail;
        }
        if (old > 1)
            strcat(result, "/");
        strcat(result, part);
        struct stat stat;
        if (lstat(result, &stat) < 0)
            goto fail;
        if (S_ISLNK(stat.st_mode)) {
            if (++links > 40) {
                errno = ELOOP;
                goto fail;
            }
            ssize_t count = readlink(result, next, 4095);
            if (count < 0)
                goto fail;
            next[count] = 0;
            if ((size_t)count + strlen(work + at) + 1 >= 4096) {
                errno = ENAMETOOLONG;
                goto fail;
            }
            strcat(next, work + at);
            if (next[0] == '/')
                strcpy(result, "/");
            else
                result[old] = 0;
            char *swap = work;
            work = next;
            next = swap;
            at = 0;
        }
    }
    free(work);
    free(next);
    return result;
fail:
    free(work);
    free(next);
    if (!resolved)
        free(result);
    return NULL;
}
