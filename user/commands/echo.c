#include "common.h"
int main(int argc, char **argv) {
    int i = 1;
    bool newline = true, escapes = false;
    while (i < argc && (!strcmp(argv[i], "-n") || !strcmp(argv[i], "-e"))) {
        if (argv[i][1] == 'n')
            newline = false;
        else
            escapes = true;
        i++;
    }
    for (int j = i; j < argc; j++) {
        if (j > i)
            putchar(' ');
        for (const char *p = argv[j]; *p; p++) {
            if (escapes && *p == '\\' && p[1]) {
                p++;
                if (*p == 'n')
                    putchar('\n');
                else if (*p == 't')
                    putchar('\t');
                else if (*p == 'r')
                    putchar('\r');
                else if (*p == 'c') {
                    newline = false;
                    break;
                } else
                    putchar(*p);
            } else
                putchar(*p);
        }
    }
    if (newline)
        putchar('\n');
    return ferror(stdout) ? 1 : 0;
}
