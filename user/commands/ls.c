#include "common.h"
static bool all, long_form;
static void display(const char *path, const char *name) {
    if (!long_form) {
        puts(name);
        return;
    }
    struct stat s;
    if (lstat(path, &s) < 0) {
        perror(path);
        return;
    }
    char permissions[11] = "----------";
    permissions[0] = S_ISDIR(s.st_mode)                ? 'd'
                     : S_ISLNK(s.st_mode)              ? 'l'
                     : S_ISCHR(s.st_mode)              ? 'c'
                     : (s.st_mode & S_IFMT) == S_IFBLK ? 'b'
                                                       : '-';
    for (unsigned i = 0; i < 9; i++)
        if (s.st_mode & (1u << (8 - i)))
            permissions[i + 1] = "rwx"[i % 3];
    if (s.st_mode & S_ISUID)
        permissions[3] = permissions[3] == 'x' ? 's' : 'S';
    if (s.st_mode & S_ISGID)
        permissions[6] = permissions[6] == 'x' ? 's' : 'S';
    if (s.st_mode & S_ISVTX)
        permissions[9] = permissions[9] == 'x' ? 't' : 'T';
    printf("%s %3lu %5u %5u %10ld %s", permissions, s.st_nlink, s.st_uid, s.st_gid, s.st_size,
           name);
    if (S_ISLNK(s.st_mode)) {
        char target[4096];
        ssize_t n = readlink(path, target, sizeof(target) - 1);
        if (n >= 0) {
            target[n] = 0;
            printf(" -> %s", target);
        }
    }
    putchar('\n');
}
static int compare(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}
static int list(const char *path) {
    struct stat s;
    if (lstat(path, &s) < 0) {
        perror(path);
        return 1;
    }
    if (!S_ISDIR(s.st_mode)) {
        display(path, path);
        return 0;
    }
    DIR *d = opendir(path);
    if (!d) {
        perror(path);
        return 1;
    }
    char **names = NULL;
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(d))) {
        if (!all && entry->d_name[0] == '.')
            continue;
        char **next = reallocarray(names, count + 1, sizeof(char *));
        if (!next) {
            closedir(d);
            return 1;
        }
        names = next;
        names[count++] = strdup(entry->d_name);
    }
    closedir(d);
    qsort(names, count, sizeof(char *), compare);
    for (size_t i = 0; i < count; i++) {
        char full[4096];
        if (snprintf(full, sizeof(full), "%s/%s", path, names[i]) >= 4096) {
            fputs("ls: path too long\n", stderr);
            free(names[i]);
            continue;
        }
        display(full, names[i]);
        free(names[i]);
    }
    free(names);
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
            if (*p == 'a')
                all = true;
            else if (*p == 'l')
                long_form = true;
            else {
                fprintf(stderr, "ls: unknown option -%c\n", *p);
                return 2;
            }
        }
    }
    if (i == argc)
        return list(".");
    for (; i < argc; i++)
        result |= list(argv[i]);
    return result;
}
