#include "common.h"
static int display(FILE *f, size_t count) {
    if (!count) {
        while (fgetc(f) != EOF) {
        }
        return ferror(f) ? 1 : 0;
    }
    if (count > 1000000) {
        errno = EINVAL;
        return 1;
    }
    char **lines = calloc(count, sizeof(char *));
    if (!lines)
        return 1;
    size_t index = 0, total = 0;
    char *line = NULL;
    size_t capacity = 0;
    while (getline(&line, &capacity, f) >= 0) {
        free(lines[index]);
        lines[index] = line;
        line = NULL;
        capacity = 0;
        index = (index + 1) % count;
        total++;
    }
    free(line);
    size_t amount = total < count ? total : count, start = total < count ? 0 : index;
    for (size_t i = 0; i < amount; i++)
        fputs(lines[(start + i) % count], stdout);
    for (size_t i = 0; i < count; i++)
        free(lines[i]);
    free(lines);
    return ferror(f) ? 1 : 0;
}
int main(int argc, char **argv) {
    size_t count = 10;
    int i = 1, status = 0;
    if (i + 1 < argc && !strcmp(argv[i], "-n")) {
        count = strtoul(argv[i + 1], NULL, 10);
        i += 2;
    }
    if (i == argc)
        return display(stdin, count);
    for (; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        if (!f) {
            perror(argv[i]);
            status = 1;
            continue;
        }
        status |= display(f, count);
        fclose(f);
    }
    return status;
}
