#include "common.h"
static bool line_only, word_only, byte_only;
static int count(int fd, const char *name) {
    unsigned long lines = 0, words = 0, bytes = 0;
    bool in_word = false;
    char b[4096];
    ssize_t n;
    while ((n = read(fd, b, sizeof(b))) > 0) {
        bytes += n;
        for (ssize_t i = 0; i < n; i++) {
            if (b[i] == '\n')
                lines++;
            if (isspace((unsigned char)b[i]))
                in_word = false;
            else if (!in_word) {
                in_word = true;
                words++;
            }
        }
    }
    if (n < 0) {
        perror(name ? name : "wc");
        return 1;
    }
    if (!line_only && !word_only && !byte_only)
        printf("%lu %lu %lu", lines, words, bytes);
    else {
        if (line_only)
            printf("%lu ", lines);
        if (word_only)
            printf("%lu ", words);
        if (byte_only)
            printf("%lu ", bytes);
    }
    if (name)
        printf(" %s", name);
    putchar('\n');
    return 0;
}
int main(int argc, char **argv) {
    int i = 1, status = 0;
    for (; i < argc && argv[i][0] == '-'; i++)
        for (char *p = argv[i] + 1; *p; p++)
            if (*p == 'l')
                line_only = true;
            else if (*p == 'w')
                word_only = true;
            else if (*p == 'c')
                byte_only = true;
            else
                return 2;
    if (i == argc)
        return count(0, NULL);
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            perror(argv[i]);
            status = 1;
            continue;
        }
        status |= count(fd, argv[i]);
        close(fd);
    }
    return status;
}
