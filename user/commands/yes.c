#include "common.h"
int main(int argc, char **argv) {
    char *line = NULL;
    size_t length = 0;
    for (int i = 1; i < argc; i++)
        length += strlen(argv[i]) + 1;
    if (!length) {
        line = strdup("y\n");
        length = 2;
    } else {
        line = malloc(length + 1);
        if (!line)
            return 1;
        line[0] = 0;
        for (int i = 1; i < argc; i++) {
            if (i > 1)
                strcat(line, " ");
            strcat(line, argv[i]);
        }
        strcat(line, "\n");
        length = strlen(line);
    }
    for (;;)
        if (output_all(1, line, length) < 0) {
            free(line);
            return 1;
        }
}
