#include "common.h"
static bool reverse, numeric;
static int compare(const void *a, const void *b) {
    const char *x = *(char *const *)a, *y = *(char *const *)b;
    int result;
    if (numeric) {
        double p = strtod(x, NULL), q = strtod(y, NULL);
        result = p < q ? -1 : p > q ? 1 : 0;
    } else
        result = strcmp(x, y);
    return reverse ? -result : result;
}
int main(int argc, char **argv) {
    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++)
        for (char *p = argv[i] + 1; *p; p++)
            if (*p == 'r')
                reverse = true;
            else if (*p == 'n')
                numeric = true;
            else
                return 2;
    FILE *f = i < argc ? fopen(argv[i], "r") : stdin;
    if (!f) {
        perror(argv[i]);
        return 1;
    }
    char **lines = NULL, *line = NULL;
    size_t count = 0, capacity = 0;
    while (getline(&line, &capacity, f) >= 0) {
        char **v = reallocarray(lines, count + 1, sizeof(char *));
        if (!v)
            return 1;
        lines = v;
        lines[count++] = line;
        line = NULL;
        capacity = 0;
    }
    free(line);
    if (f != stdin)
        fclose(f);
    qsort(lines, count, sizeof(char *), compare);
    for (size_t j = 0; j < count; j++) {
        fputs(lines[j], stdout);
        size_t n = strlen(lines[j]);
        if (!n || lines[j][n - 1] != '\n')
            putchar('\n');
        free(lines[j]);
    }
    free(lines);
    return 0;
}
