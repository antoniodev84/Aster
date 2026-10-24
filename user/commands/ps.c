#include "common.h"
int main(void) {
    DIR *d = opendir("/proc");
    if (!d) {
        perror("ps");
        return 1;
    }
    puts("PID   PPID  STATE NAME");
    struct dirent *entry;
    while ((entry = readdir(d))) {
        if (!isdigit(entry->d_name[0]))
            continue;
        char path[256];
        snprintf(path, sizeof(path), "/proc/%s/status", entry->d_name);
        size_t length;
        char *data = load_file(path, &length);
        if (!data)
            continue;
        char name[80] = "?", state[8] = "?";
        int pid = 0, ppid = 0;
        char *save = NULL;
        for (char *line = strtok_r(data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
            if (!strncmp(line, "Name:\t", 6))
                snprintf(name, sizeof(name), "%s", line + 6);
            else if (!strncmp(line, "Pid:\t", 5))
                pid = atoi(line + 5);
            else if (!strncmp(line, "PPid:\t", 6))
                ppid = atoi(line + 6);
            else if (!strncmp(line, "State:\t", 7))
                snprintf(state, sizeof(state), "%s", line + 7);
        }
        printf("%-5d %-5d %-5s %s\n", pid, ppid, state, name);
        free(data);
    }
    closedir(d);
    return 0;
}
