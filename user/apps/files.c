#include "gui.h"
struct entry {
    char name[256];
    int directory;
};
static struct entry *entries;
static int count, selected = -1, scroll, preview;
static char path[1024] = "/", edit[1024] = "/", name[128], message[160], contents[1500];
static void refresh(void) {
    free(entries);
    entries = NULL;
    count = 0;
    selected = -1;
    scroll = 0;
    preview = 0;
    DIR *d = opendir(path);
    if (!d) {
        snprintf(message, sizeof(message), "%s", strerror(errno));
        return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        struct entry *next = realloc(entries, (count + 1) * sizeof(*entries));
        if (!next) {
            snprintf(message, sizeof(message), "Out of memory");
            break;
        }
        entries = next;
        snprintf(entries[count].name, sizeof(entries[count].name), "%s", e->d_name);
        char full[1400];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        entries[count].directory = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        count++;
    }
    closedir(d);
    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++)
            if ((entries[j].directory > entries[i].directory) ||
                (entries[j].directory == entries[i].directory &&
                 strcmp(entries[i].name, entries[j].name) > 0)) {
                struct entry t = entries[i];
                entries[i] = entries[j];
                entries[j] = t;
            }
}
static void open_selected(void) {
    if (selected < 0 || selected >= count)
        return;
    char full[1400];
    snprintf(full, sizeof(full), "%s/%s", path, entries[selected].name);
    if (entries[selected].directory) {
        char normalized[4096];
        if (!realpath(full, normalized) || strlen(normalized) >= sizeof(path)) {
            snprintf(message, sizeof(message), "Path too long or inaccessible");
            return;
        }
        strcpy(path, normalized);
        strcpy(edit, path);
        refresh();
    } else {
        int fd = open(full, O_RDONLY);
        if (fd < 0) {
            snprintf(message, sizeof(message), "%s", strerror(errno));
            return;
        }
        ssize_t n = read(fd, contents, sizeof(contents) - 1);
        close(fd);
        if (n < 0) {
            snprintf(message, sizeof(message), "%s", strerror(errno));
            return;
        }
        for (ssize_t i = 0; i < n; i++)
            if ((unsigned char)contents[i] < 32 && contents[i] != '\n' && contents[i] != '\t')
                contents[i] = '.';
        contents[n] = 0;
        preview = 1;
    }
}
int main(void) {
    struct gui g;
    if (gui_open(&g) < 0) {
        perror("files");
        return 1;
    }
    refresh();
    int quit = 0, field = 0;
    while (!quit) {
        struct gui_event e;
        do {
            int got = gui_next(&g, &e);
            if (got && e.type == GUI_CLOSE)
                quit = 1;
            if (gui_hit(&g, 8, 8, 370, 26))
                field = 0;
            if (gui_hit(&g, 8, 250, 200, 26))
                field = 1;
            gui_clear(&g, GUI_BG);
            int go = gui_textbox(&g, 8, 8, 370, edit, sizeof(edit), field == 0);
            if (gui_button(&g, 388, 8, 84, "Go") || go) {
                struct stat st;
                if (stat(edit, &st) < 0 || !S_ISDIR(st.st_mode))
                    snprintf(message, sizeof(message), "Invalid directory");
                else {
                    snprintf(path, sizeof(path), "%s", edit);
                    message[0] = 0;
                    refresh();
                }
            }
            if (gui_button(&g, 8, 42, 68, "Up")) {
                char *slash = strrchr(path, '/');
                if (slash && slash != path)
                    *slash = 0;
                else
                    strcpy(path, "/");
                strcpy(edit, path);
                refresh();
            }
            if (gui_button(&g, 84, 42, 100, "Refresh"))
                refresh();
            if (gui_button(&g, 192, 42, 80, preview ? "Back" : "Open")) {
                if (preview)
                    preview = 0;
                else
                    open_selected();
            }
            if (gui_button(&g, 280, 42, 90, "Delete") && selected >= 0) {
                char full[1400];
                snprintf(full, sizeof(full), "%s/%s", path, entries[selected].name);
                int result = entries[selected].directory ? rmdir(full) : unlink(full);
                snprintf(message, sizeof(message), "%s", result < 0 ? strerror(errno) : "Deleted");
                refresh();
            }
            if (preview)
                gui_text(&g, 10, 78, contents, GUI_FG);
            else {
                for (int i = 0; i < 9 && scroll + i < count; i++) {
                    int id = scroll + i;
                    if (gui_hit(&g, 8, 76 + i * 18, 420, 18)) {
                        selected = id;
                        field = -1;
                    }
                    if (id == selected)
                        gui_fill(&g, 8, 76 + i * 18, 420, 18, 0x236777);
                    char label[53];
                    snprintf(label, sizeof(label), "%c %s", entries[id].directory ? '+' : ' ',
                             entries[id].name);
                    gui_text(&g, 12, 77 + i * 18, label, GUI_FG);
                }
                if (gui_button(&g, 432, 80, 40, "^") && scroll > 0)
                    scroll--;
                if (gui_button(&g, 432, 210, 40, "v") && scroll + 9 < count)
                    scroll++;
            }
            gui_fill(&g, 0, 246, 480, 74, GUI_BG);
            gui_textbox(&g, 8, 250, 200, name, sizeof(name), field == 1);
            if (gui_button(&g, 216, 250, 118, "New folder") && name[0] && !strchr(name, '/')) {
                char full[1400];
                snprintf(full, sizeof(full), "%s/%s", path, name);
                int result = mkdir(full, 0755);
                snprintf(message, sizeof(message), "%s",
                         result < 0 ? strerror(errno) : "Folder created");
                refresh();
            }
            if (gui_button(&g, 342, 250, 130, "Rename") && selected >= 0 && name[0] &&
                !strchr(name, '/')) {
                char from[1400], to[1400];
                snprintf(from, sizeof(from), "%s/%s", path, entries[selected].name);
                snprintf(to, sizeof(to), "%s/%s", path, name);
                int result = rename(from, to);
                snprintf(message, sizeof(message), "%s", result < 0 ? strerror(errno) : "Renamed");
                refresh();
            }
            gui_text(&g, 8, 290, message, GUI_FG);
            gui_present(&g);
            if (!got)
                break;
        } while (!quit);
        usleep(40000);
    }
    free(entries);
    gui_close(&g);
    return 0;
}
