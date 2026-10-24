#include "common.h"
static int display(FILE *f, unsigned long count) {
    char *line = NULL;
    size_t capacity = 0;
    while (count-- && getline(&line, &capacity, f) >= 0)
        fputs(line, stdout);
    free(line);
    return ferror(f) ? 1 : 0;
}
int main(int argc, char **argv) {
    unsigned long n = 10;
    int i = 1, status = 0;
    if (i + 1 < argc && !strcmp(argv[i], "-n")) {
        n = strtoul(argv[i + 1], NULL, 10);
        i += 2;
    }
    if (i == argc)
        return display(stdin, n);
    for (; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) {
            perror(argv[i]);
            status = 1;
            continue;
        }
        status |= display(f, n);
        fclose(f);
    }
    return status;
}
