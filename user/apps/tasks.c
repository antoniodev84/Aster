#include "gui.h"
struct task {
    int pid;
    char info[80];
};
static struct task *tasks;
static int count, scroll, selected = -1;
static char memory[160], message[100];
static void refresh(void) {
    free(tasks);
    tasks = NULL;
    count = 0;
    DIR *d = opendir("/proc");
    if (!d) {
        snprintf(message, sizeof(message), "%s", strerror(errno));
        return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!isdigit(e->d_name[0]))
            continue;
        char path[128], text[512];
        snprintf(path, sizeof(path), "/proc/%s/status", e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;
        ssize_t n = read(fd, text, sizeof(text) - 1);
        close(fd);
        if (n < 0)
            continue;
        text[n] = 0;
        char *end = strchr(text, '\n');
        if (end)
            *end = 0;
        char *state = end ? strstr(end + 1, "State:\t") : NULL;
        struct task *next = realloc(tasks, (count + 1) * sizeof(*tasks));
        if (!next)
            break;
        tasks = next;
        tasks[count].pid = atoi(e->d_name);
        snprintf(tasks[count].info, sizeof(tasks[count].info), "%5d   %c   %s", tasks[count].pid,
                 state ? state[7] : '?', !strncmp(text, "Name:\t", 6) ? text + 6 : text);
        count++;
    }
    closedir(d);
    int fd = open("/proc/meminfo", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, memory, sizeof(memory) - 1);
        close(fd);
        if (n >= 0)
            memory[n] = 0;
    }
    if (scroll >= count)
        scroll = count > 7 ? count - 7 : 0;
}
int main(void) {
    struct gui g;
    if (gui_open(&g) < 0) {
        perror("tasks");
        return 1;
    }
    int quit = 0;
    uint64_t last = 0;
    while (!quit) {
        if (gui_millis() - last >= 1000) {
            refresh();
            last = gui_millis();
        }
        struct gui_event e;
        do {
            int got = gui_next(&g, &e);
            if (got && e.type == GUI_CLOSE)
                quit = 1;
            gui_clear(&g, GUI_BG);
            gui_text(&g, 12, 10, memory, GUI_FG);
            gui_text(&g, 12, 62, " PID   State  Process", GUI_ACCENT);
            for (int i = 0; i < 9 && scroll + i < count; i++) {
                struct task *t = &tasks[scroll + i];
                if (gui_hit(&g, 8, 86 + i * 18, 420, 18))
                    selected = t->pid;
                if (t->pid == selected)
                    gui_fill(&g, 8, 86 + i * 18, 420, 18, 0x236777);
                gui_text(&g, 12, 86 + i * 18, t->info, GUI_FG);
            }
            if (gui_button(&g, 436, 86, 36, "^") && scroll)
                scroll--;
            if (gui_button(&g, 436, 218, 36, "v") && scroll + 9 < count)
                scroll++;
            if (gui_button(&g, 8, 256, 130, "Refresh"))
                refresh();
            int terminate = gui_button(&g, 146, 256, 150, "Terminate"),
                force = gui_button(&g, 304, 256, 168, "Force kill");
            if ((terminate || force) && selected > 1 && selected != getpid() &&
                selected != getppid()) {
                int result = kill(selected, force ? SIGKILL : SIGTERM);
                snprintf(message, sizeof(message), "%s",
                         result < 0 ? strerror(errno) : "Signal sent");
                refresh();
            }
            gui_text(&g, 8, 294, message, GUI_FG);
            gui_present(&g);
            if (!got)
                break;
        } while (!quit);
        usleep(50000);
    }
    free(tasks);
    gui_close(&g);
    return 0;
}
