#include "common.h"
static bool insensitive, invert, number;
static const char *needle;
static bool contains(const char *line) {
    if (!insensitive)
        return strstr(line, needle) != NULL;
    size_t n = strlen(needle);
    do {
        if (!strncasecmp(line, needle, n))
            return true;
    } while (*line++);
    return false;
}
static int search(FILE *f, const char *name, bool prefix) {
    char *line = NULL;
    size_t capacity = 0;
    unsigned long index = 0;
    bool matched = false;
    while (getline(&line, &capacity, f) >= 0) {
        index++;
        if (contains(line) != invert) {
            matched = true;
            if (prefix)
                printf("%s:", name);
            if (number)
                printf("%lu:", index);
            fputs(line, stdout);
            size_t n = strlen(line);
            if (!n || line[n - 1] != '\n')
                putchar('\n');
        }
    }
    free(line);
    return ferror(f) ? 2 : matched ? 0 : 1;
}
int main(int argc, char **argv) {
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++)
        for (char *p = argv[i] + 1; *p; p++)
            if (*p == 'i')
                insensitive = true;
            else if (*p == 'v')
                invert = true;
            else if (*p == 'n')
                number = true;
            else if (*p != 'F')
                return 2;
    if (i == argc) {
        fputs("usage: grep [-invF] literal [file...]\n", stderr);
        return 2;
    }
    needle = argv[i++];
    if (i == argc)
        return search(stdin, NULL, false);
    int result = 1, total = argc - i;
    for (; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) {
            perror(argv[i]);
            result = 2;
            continue;
        }
        int status = search(f, argv[i], total > 1);
        if (status == 2)
            result = 2;
        else if (!status && result != 2)
            result = 0;
        fclose(f);
    }
    return result;
}
