#include "gui.h"
#define WINDOWS 16
struct window {
    pid_t pid;
    int fd, x, y, hidden, closing;
    uint64_t deadline;
    char title[128];
    struct gui_shared *shared;
    uint32_t *front;
    uint32_t sequence;
};
static struct window windows[WINDOWS];
static int order[WINDOWS], count, focus = -1, mx = 100, my = 100, buttons;
static int dragging = -1, dragx, dragy, menu, menu_scroll, menu_selected;
static struct gui screen;
static struct abi_fbinfo fbinfo;
static int fb = -1, mouse = -1, keyboard = -1;
static char **apps;
static size_t app_count;
static char notice[128];
static volatile int running = 1;
static void stop(int s) {
    (void)s;
    running = 0;
}
static void app_list(void) {
    for (size_t i = 0; i < app_count; i++)
        free(apps[i]);
    free(apps);
    apps = NULL;
    app_count = 0;
    DIR *dir = opendir("/usr/apps/bin");
    if (!dir) {
        snprintf(notice, sizeof(notice), "Apps: %s", strerror(errno));
        return;
    }
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char path[512];
        struct stat st;
        snprintf(path, sizeof(path), "/usr/apps/bin/%s", e->d_name);
        if (stat(path, &st) < 0 || !S_ISREG(st.st_mode) || access(path, X_OK))
            continue;
        char **next = realloc(apps, (app_count + 1) * sizeof(*apps));
        if (!next) {
            snprintf(notice, sizeof(notice), "Apps: out of memory");
            break;
        }
        apps = next;
        apps[app_count] = strdup(e->d_name);
        if (!apps[app_count])
            break;
        app_count++;
    }
    closedir(dir);
    for (size_t i = 0; i < app_count; i++)
        for (size_t j = i + 1; j < app_count; j++)
            if (strcmp(apps[i], apps[j]) > 0) {
                char *s = apps[i];
                apps[i] = apps[j];
                apps[j] = s;
            }
}
static void event_to(int id, struct gui_event e) {
    if (id < 0 || !windows[id].pid || windows[id].closing)
        return;
    struct gui_shared *s = windows[id].shared;
    unsigned head = __atomic_load_n(&s->head, __ATOMIC_RELAXED) % GUI_EVENTS;
    unsigned next = (head + 1) % GUI_EVENTS;
    if (next == __atomic_load_n(&s->tail, __ATOMIC_ACQUIRE) % GUI_EVENTS)
        return;
    s->events[head] = e;
    __atomic_store_n(&s->head, next, __ATOMIC_RELEASE);
}
static void activate(int id) {
    if (focus != id) {
        event_to(focus, (struct gui_event){.type = GUI_FOCUS, .pressed = 0});
        focus = id;
        event_to(id, (struct gui_event){.type = GUI_FOCUS, .pressed = 1});
    }
    if (id < 0)
        return;
    windows[id].hidden = 0;
    for (int i = 0; i < count; i++)
        if (order[i] == id) {
            memmove(order + i, order + i + 1, (count - i - 1) * sizeof(int));
            order[count - 1] = id;
            break;
        }
}
static void launch(const char *name) {
    int id;
    for (id = 0; id < WINDOWS && windows[id].pid; id++)
        ;
    if (id == WINDOWS) {
        snprintf(notice, sizeof(notice), "Window limit reached (16)");
        return;
    }
    char path[512], temp[128];
    snprintf(path, sizeof(path), "/usr/apps/bin/%s", name);
    snprintf(temp, sizeof(temp), "/dev/shm/asterwm-%d-%d", getpid(), id);
    int fd = open(temp, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0)
        goto error;
    unlink(temp);
    if (ftruncate(fd, sizeof(struct gui_shared)) < 0) {
        close(fd);
        goto error;
    }
    struct gui_shared *s = mmap(NULL, sizeof(*s), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (s == MAP_FAILED) {
        close(fd);
        goto error;
    }
    uint32_t *front = calloc(GUI_WIDTH * GUI_HEIGHT, 4);
    if (!front) {
        munmap(s, sizeof(*s));
        close(fd);
        goto error;
    }
    memset(s, 0, sizeof(*s));
    s->magic = GUI_MAGIC;
    s->width = GUI_WIDTH;
    s->height = GUI_HEIGHT;
    pid_t child = fork();
    if (child < 0) {
        free(front);
        munmap(s, sizeof(*s));
        close(fd);
        goto error;
    }
    if (!child) {
        for (int i = 3; i < 128; i++)
            if (i != fd)
                close(i);
        int log = open("/tmp/aster-app.log", O_CREAT | O_WRONLY | O_APPEND, 0600);
        int input = open("/dev/null", O_RDONLY);
        if (input >= 0) {
            dup2(input, 0);
            close(input);
        }
        if (log >= 0) {
            dup2(log, 1);
            dup2(log, 2);
            if (log > 2)
                close(log);
        }
        char descriptor[24];
        snprintf(descriptor, sizeof(descriptor), "%d", fd);
        setenv("ASTER_SURFACE_FD", descriptor, 1);
        setpgid(0, 0);
        execl(path, name, (char *)NULL);
        perror(path);
        _exit(127);
    }
    setpgid(child, child);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    struct window *w = &windows[id];
    *w = (struct window){.pid = child,
                         .fd = fd,
                         .shared = s,
                         .front = front,
                         .x = 30 + (id * 38) % (screen.width - GUI_WIDTH - 40),
                         .y = 45 + (id * 28) % (screen.height - GUI_HEIGHT - 105)};
    snprintf(w->title, sizeof(w->title), "%s", name);
    order[count++] = id;
    activate(id);
    return;
error:
    snprintf(notice, sizeof(notice), "Launch: %s", strerror(errno));
}
static void reap(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int id = 0; id < WINDOWS; id++)
            if (windows[id].pid == pid) {
                struct window *w = &windows[id];
                if (status && !w->closing)
                    snprintf(notice, sizeof(notice), "%s exited (%d)", w->title, status);
                kill(-pid, SIGKILL);
                munmap(w->shared, sizeof(*w->shared));
                close(w->fd);
                free(w->front);
                memset(w, 0, sizeof(*w));
                for (int i = 0; i < count; i++)
                    if (order[i] == id) {
                        memmove(order + i, order + i + 1, (count - i - 1) * sizeof(int));
                        count--;
                        break;
                    }
                if (dragging == id)
                    dragging = -1;
                if (focus == id) {
                    focus = -1;
                    for (int i = count - 1; i >= 0; i--)
                        if (!windows[order[i]].hidden) {
                            activate(order[i]);
                            break;
                        }
                }
                break;
            }
    }
    for (int id = 0; id < WINDOWS; id++)
        if (windows[id].closing && gui_millis() >= windows[id].deadline) {
            kill(-windows[id].pid, SIGKILL);
            windows[id].deadline = UINT64_MAX;
        }
}
static int task_width(void) {
    int width = count ? (screen.width - 206) / count : 132;
    return width < 132 ? width : 132;
}
static int menu_rows(void) {
    return (screen.height - 150) / 28;
}
static void pointer_event(struct abi_mouse_event m) {
    mx += m.dx;
    my += m.dy;
    if (mx < 0)
        mx = 0;
    if (my < 0)
        my = 0;
    if (mx >= screen.width)
        mx = screen.width - 1;
    if (my >= screen.height)
        my = screen.height - 1;
    int pressed = (m.buttons & 1) && !(buttons & 1);
    int released = !(m.buttons & 1) && (buttons & 1);
    buttons = m.buttons;
    if (dragging >= 0) {
        if (!(buttons & 1))
            dragging = -1;
        else {
            struct window *w = &windows[dragging];
            w->x = mx - dragx;
            w->y = my - dragy;
            if (w->x < 0)
                w->x = 0;
            if (w->y < 28)
                w->y = 28;
            if (w->x > screen.width - GUI_WIDTH - 4)
                w->x = screen.width - GUI_WIDTH - 4;
            if (w->y > screen.height - 90)
                w->y = screen.height - 90;
        }
        return;
    }
    if (pressed && my >= screen.height - 36) {
        if (mx < 100) {
            menu = !menu;
            menu_scroll = menu_selected = 0;
            if (menu)
                app_list();
            return;
        }
        if (mx >= screen.width - 90) {
            running = 0;
            return;
        }
        int slot = (mx - 106) / task_width();
        if (slot >= 0 && slot < count) {
            int id = order[slot];
            if (focus == id && !windows[id].hidden) {
                windows[id].hidden = 1;
                activate(-1);
            } else
                activate(id);
        }
        return;
    }
    if (menu && pressed) {
        int rows = menu_rows(), top = screen.height - 78 - rows * 28;
        if (mx < 256 && my >= top && my < screen.height - 78) {
            size_t index = menu_scroll + (my - top) / 28;
            if (index < app_count)
                launch(apps[index]);
            menu = 0;
            return;
        }
        if (mx < 256 && my >= screen.height - 78) {
            if (mx < 128 && menu_scroll > 0)
                menu_scroll--;
            else if (mx >= 128 && (size_t)(menu_scroll + rows) < app_count)
                menu_scroll++;
            return;
        }
        menu = 0;
    }
    if (pressed) {
        int hit = -1;
        for (int i = count - 1; i >= 0; i--) {
            int id = order[i];
            struct window *w = &windows[id];
            if (!w->hidden && mx >= w->x && mx < w->x + GUI_WIDTH + 4 && my >= w->y &&
                my < w->y + GUI_HEIGHT + 30) {
                hit = id;
                break;
            }
        }
        activate(hit);
        if (hit >= 0) {
            struct window *w = &windows[hit];
            if (my < w->y + 28) {
                if (mx >= w->x + GUI_WIDTH - 26) {
                    event_to(hit, (struct gui_event){.type = GUI_CLOSE});
                    w->closing = 1;
                    w->deadline = gui_millis() + 1500;
                } else if (mx >= w->x + GUI_WIDTH - 58) {
                    w->hidden = 1;
                    activate(-1);
                } else {
                    dragging = hit;
                    dragx = mx - w->x;
                    dragy = my - w->y;
                }
                return;
            }
        }
    }
    if (focus >= 0 && !windows[focus].hidden) {
        struct window *w = &windows[focus];
        int x = mx - w->x - 2, y = my - w->y - 28;
        if ((x >= 0 && x < GUI_WIDTH && y >= 0 && y < GUI_HEIGHT) || released)
            event_to(
                focus,
                (struct gui_event){
                    .type = GUI_MOUSE, .x = x, .y = y, .buttons = buttons, .pressed = pressed});
    }
}
static void render(void) {
    gui_clear(&screen, 0x102d38);
    gui_text(&screen, 24, 8, "ASTER  /  userland desktop", 0x89bbbd);
    gui_text(&screen, 24, screen.height - 68, notice, GUI_FG);
    for (int i = 0; i < count; i++) {
        int id = order[i];
        struct window *w = &windows[id];
        if (w->hidden)
            continue;
        unsigned seq = __atomic_load_n(&w->shared->sequence, __ATOMIC_ACQUIRE);
        if (!(seq & 1) && seq != w->sequence) {
            static uint32_t snapshot[GUI_WIDTH * GUI_HEIGHT];
            memcpy(snapshot, w->shared->pixels, sizeof(snapshot));
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if (seq == __atomic_load_n(&w->shared->sequence, __ATOMIC_ACQUIRE)) {
                memcpy(w->front, snapshot, sizeof(snapshot));
                w->sequence = seq;
            }
        }
        gui_fill(&screen, w->x + 5, w->y + 6, GUI_WIDTH + 4, GUI_HEIGHT + 30, 0x091820);
        gui_fill(&screen, w->x, w->y, GUI_WIDTH + 4, GUI_HEIGHT + 30,
                 id == focus ? GUI_ACCENT : 0x40535f);
        gui_fill(&screen, w->x + 2, w->y + 2, GUI_WIDTH, 24, id == focus ? 0x236777 : 0x293c4b);
        gui_text(&screen, w->x + 10, w->y + 6, w->title, GUI_FG);
        gui_text(&screen, w->x + GUI_WIDTH - 50, w->y + 6, "_", GUI_FG);
        gui_text(&screen, w->x + GUI_WIDTH - 18, w->y + 6, "x", GUI_FG);
        for (int y = 0; y < GUI_HEIGHT && w->y + 28 + y < screen.height - 36; y++)
            memcpy(screen.pixels + (w->y + 28 + y) * screen.width + w->x + 2,
                   w->front + y * GUI_WIDTH, GUI_WIDTH * 4);
    }
    gui_fill(&screen, 0, screen.height - 36, screen.width, 36, 0x0c1b25);
    gui_fill(&screen, 4, screen.height - 32, 94, 28, GUI_ACCENT);
    gui_text(&screen, 15, screen.height - 26, "Start", 0x071923);
    for (int i = 0; i < count; i++) {
        int id = order[i];
        gui_fill(&screen, 106 + i * task_width(), screen.height - 32, task_width() - 6, 28,
                 id == focus ? 0x236777 : 0x293c4b);
        char title[15];
        snprintf(title, sizeof(title), "%.*s", (task_width() - 16) / 8, windows[id].title);
        gui_text(&screen, 112 + i * task_width(), screen.height - 26, title, GUI_FG);
    }
    gui_text(&screen, screen.width - 76, screen.height - 26, "Exit", GUI_FG);
    if (menu) {
        int rows = menu_rows(), top = screen.height - 78 - rows * 28;
        gui_fill(&screen, 4, top - 28, 252, rows * 28 + 68, 0x243e4d);
        gui_text(&screen, 14, top - 22, "Applications", GUI_ACCENT);
        for (int i = 0; i < rows && (size_t)(menu_scroll + i) < app_count; i++) {
            if (menu_scroll + i == menu_selected ||
                (my >= top + i * 28 && my < top + i * 28 + 28 && mx < 256))
                gui_fill(&screen, 8, top + i * 28, 244, 28, 0x236777);
            char title[29];
            snprintf(title, sizeof(title), "%s", apps[menu_scroll + i]);
            gui_text(&screen, 16, top + i * 28 + 6, title, GUI_FG);
        }
        gui_text(&screen, 16, screen.height - 68, "^ Previous      Next v", GUI_FG);
    }
    for (int y = 0; y < 15; y++)
        for (int x = 0; x <= y / 2; x++)
            gui_fill(&screen, mx + x, my + y, 1, 1, x == 0 || x == y / 2 ? 0x071923 : 0xffffff);
    static uint8_t *previous;
    static uint32_t channels[3][256];
    static int initialized;
    if (!previous)
        previous = malloc((size_t)fbinfo.pitch * fbinfo.height);
    if (!previous) {
        running = 0;
        return;
    }
    if (!initialized) {
        memset(previous, 255, (size_t)fbinfo.pitch * fbinfo.height);
        for (unsigned c = 0; c < 256; c++) {
            channels[0][c] = c * ((1u << fbinfo.red_size) - 1) / 255 << fbinfo.red_shift;
            channels[1][c] = c * ((1u << fbinfo.green_size) - 1) / 255 << fbinfo.green_shift;
            channels[2][c] = c * ((1u << fbinfo.blue_size) - 1) / 255 << fbinfo.blue_shift;
        }
        initialized = 1;
    }
    int native = fbinfo.red_size == 8 && fbinfo.green_size == 8 && fbinfo.blue_size == 8 &&
                 fbinfo.red_shift == 16 && fbinfo.green_shift == 8 && fbinfo.blue_shift == 0;
    uint32_t converted[4096];
    for (int y = 0; y < screen.height; y++) {
        uint32_t *line = screen.pixels + y * screen.width;
        if (!native) {
            for (int x = 0; x < screen.width; x++) {
                uint32_t c = line[x];
                converted[x] = channels[0][(c >> 16) & 255] | channels[1][(c >> 8) & 255] |
                               channels[2][c & 255];
            }
            line = converted;
        }
        uint8_t *old = previous + (size_t)y * fbinfo.pitch;
        if (!memcmp(old, line, screen.width * 4))
            continue;
        int first = 0, last = screen.width;
        while (first < last && ((uint32_t *)old)[first] == line[first])
            first++;
        while (last > first && ((uint32_t *)old)[last - 1] == line[last - 1])
            last--;
        if (lseek(fb, (size_t)y * fbinfo.pitch + first * 4, SEEK_SET) < 0) {
            running = 0;
            return;
        }
        size_t done = 0, size = (last - first) * 4;
        const uint8_t *bytes = (const uint8_t *)(line + first);
        while (done < size) {
            ssize_t n = write(fb, bytes + done, size - done);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0) {
                running = 0;
                return;
            }
            done += n;
        }
        memcpy(old + first * 4, bytes, size);
    }
}
int main(void) {
    fb = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fb < 0) {
        perror("asterwm: fb0");
        return 1;
    }
    if (ioctl(fb, FB_GETINFO, &fbinfo) < 0 || fbinfo.bpp != 32 || fbinfo.width < 640 ||
        fbinfo.height < 480 || fbinfo.width > 4096 || fbinfo.height > 2160) {
        close(fb);
        fprintf(stderr, "asterwm: unsupported framebuffer\n");
        return 1;
    }
    screen.width = fbinfo.width;
    screen.height = fbinfo.height;
    screen.pixels = calloc((size_t)screen.width * screen.height, 4);
    mouse = open("/dev/mouse0", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    keyboard = open("/dev/kbd0", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (!screen.pixels || mouse < 0 || keyboard < 0) {
        close(fb);
        perror("asterwm: input/allocation");
        return 1;
    }
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    signal(SIGHUP, stop);
    signal(SIGPIPE, SIG_IGN);
    printf("[asterwm] framebuffer=%ux%u mouse0/kbd0 ready\n", fbinfo.width, fbinfo.height);
    app_list();
    launch("clock");
    while (running) {
        reap();
        struct abi_mouse_event m[32];
        ssize_t n;
        while ((n = read(mouse, m, sizeof(m))) > 0)
            for (size_t i = 0; i < (size_t)n / sizeof(*m); i++)
                pointer_event(m[i]);
        if (n < 0 && errno != EAGAIN && errno != EINTR) {
            snprintf(notice, sizeof(notice), "mouse0: %s", strerror(errno));
            running = 0;
        }
        struct abi_key_event k[32];
        while ((n = read(keyboard, k, sizeof(k))) > 0)
            for (size_t i = 0; i < (size_t)n / sizeof(*k); i++) {
                if (k[i].pressed && k[i].code == 0x76) {
                    menu = !menu;
                    if (menu) {
                        app_list();
                        menu_selected = menu_scroll = 0;
                    }
                } else if (menu && k[i].pressed) {
                    if (k[i].code == 0x175 && menu_selected > 0)
                        menu_selected--;
                    if (k[i].code == 0x172 && (size_t)(menu_selected + 1) < app_count)
                        menu_selected++;
                    if (menu_selected < menu_scroll)
                        menu_scroll = menu_selected;
                    if (menu_selected >= menu_scroll + menu_rows())
                        menu_scroll = menu_selected - menu_rows() + 1;
                    if (k[i].character == '\n' && app_count) {
                        launch(apps[menu_selected]);
                        menu = 0;
                    }
                } else
                    event_to(focus, (struct gui_event){.type = GUI_KEY,
                                                       .code = k[i].code,
                                                       .pressed = k[i].pressed,
                                                       .character = k[i].character});
            }
        if (n < 0 && errno != EAGAIN && errno != EINTR)
            running = 0;
        render();
        usleep(20000);
    }
    for (int i = 0; i < WINDOWS; i++)
        if (windows[i].pid && !windows[i].closing) {
            event_to(i, (struct gui_event){.type = GUI_CLOSE});
            windows[i].closing = 1;
            windows[i].deadline = gui_millis() + 1500;
        }
    while (count) {
        reap();
        usleep(1000);
    }
    close(mouse);
    close(keyboard);
    close(fb);
    free(screen.pixels);
    for (size_t i = 0; i < app_count; i++)
        free(apps[i]);
    free(apps);
    puts("\033[2J\033[Hasterwm exited");
    return 0;
}
