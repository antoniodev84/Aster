#include "gui.h"
#include "font.h"
int gui_open(struct gui *g) {
    memset(g, 0, sizeof(*g));
    g->fd = -1;
    const char *s = getenv("ASTER_SURFACE_FD");
    if (!s) {
        errno = ENOTCONN;
        return -1;
    }
    char *end;
    long fd = strtol(s, &end, 10);
    struct stat st;
    if (*end || fd < 3 || fd > 127 || fstat(fd, &st) < 0 ||
        st.st_size != sizeof(struct gui_shared)) {
        errno = EINVAL;
        return -1;
    }
    g->shared = mmap(NULL, sizeof(struct gui_shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (g->shared == MAP_FAILED) {
        g->shared = NULL;
        return -1;
    }
    if (g->shared->magic != GUI_MAGIC || g->shared->width != GUI_WIDTH ||
        g->shared->height != GUI_HEIGHT) {
        munmap(g->shared, sizeof(struct gui_shared));
        g->shared = NULL;
        errno = EINVAL;
        return -1;
    }
    g->pixels = calloc(GUI_WIDTH * GUI_HEIGHT, sizeof(uint32_t));
    if (!g->pixels) {
        munmap(g->shared, sizeof(struct gui_shared));
        g->shared = NULL;
        return -1;
    }
    g->width = GUI_WIDTH;
    g->height = GUI_HEIGHT;
    g->fd = fd;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    unsetenv("ASTER_SURFACE_FD");
    return 0;
}
void gui_close(struct gui *g) {
    if (g->shared)
        munmap(g->shared, sizeof(struct gui_shared));
    if (g->fd >= 0)
        close(g->fd);
    free(g->pixels);
    memset(g, 0, sizeof(*g));
    g->fd = -1;
}
int gui_next(struct gui *g, struct gui_event *e) {
    uint32_t tail = __atomic_load_n(&g->shared->tail, __ATOMIC_RELAXED) % GUI_EVENTS;
    if (tail == __atomic_load_n(&g->shared->head, __ATOMIC_ACQUIRE) % GUI_EVENTS) {
        memset(&g->event, 0, sizeof(g->event));
        return 0;
    }
    *e = g->shared->events[tail];
    __atomic_store_n(&g->shared->tail, (tail + 1) % GUI_EVENTS, __ATOMIC_RELEASE);
    g->event = *e;
    return 1;
}
void gui_present(struct gui *g) {
    if (!memcmp(g->shared->pixels, g->pixels, GUI_WIDTH * GUI_HEIGHT * 4))
        return;
    __atomic_add_fetch(&g->shared->sequence, 1, __ATOMIC_ACQ_REL);
    memcpy(g->shared->pixels, g->pixels, GUI_WIDTH * GUI_HEIGHT * 4);
    __atomic_add_fetch(&g->shared->sequence, 1, __ATOMIC_RELEASE);
}
void gui_fill(struct gui *g, int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0)
        return;
    int64_t right = (int64_t)x + w, bottom = (int64_t)y + h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (right > g->width)
        right = g->width;
    if (bottom > g->height)
        bottom = g->height;
    for (int j = y; j < bottom; j++)
        for (int i = x; i < right; i++)
            g->pixels[j * g->width + i] = c;
}
void gui_clear(struct gui *g, uint32_t c) {
    gui_fill(g, 0, 0, g->width, g->height, c);
}
void gui_text(struct gui *g, int x, int y, const char *s, uint32_t c) {
    int origin = x;
    for (; *s && y < g->height; s++) {
        unsigned char ch = *s;
        if (ch == '\n') {
            y += 16;
            x = origin;
            continue;
        }
        if (ch == '\t') {
            x += 32;
            continue;
        }
        if (ch >= 128)
            ch = '?';
        if (x > g->width - 8) {
            x = origin;
            y += 16;
        }
        for (int j = 0; j < 16; j++)
            for (int i = 0; i < 8; i++)
                if (font8x8_basic[ch][j / 2] & (1 << i))
                    gui_fill(g, x + i, y + j, 1, 1, c);
        x += 8;
    }
}
int gui_hit(struct gui *g, int x, int y, int w, int h) {
    struct gui_event *e = &g->event;
    return e->type == GUI_MOUSE && e->pressed && (e->buttons & 1) && e->x >= x && e->y >= y &&
           e->x < x + w && e->y < y + h;
}
int gui_button(struct gui *g, int x, int y, int w, const char *label) {
    gui_fill(g, x, y, w, 26, 0x314657);
    gui_fill(g, x, y, 3, 26, GUI_ACCENT);
    gui_text(g, x + 9, y + 5, label, GUI_FG);
    return gui_hit(g, x, y, w, 26);
}
int gui_checkbox(struct gui *g, int x, int y, const char *label, int *value) {
    if (gui_hit(g, x, y, 24 + strlen(label) * 8, 24))
        *value = !*value;
    gui_fill(g, x, y, 18, 18, *value ? GUI_ACCENT : 0x314657);
    if (*value)
        gui_text(g, x + 5, y, "x", GUI_FG);
    gui_text(g, x + 26, y, label, GUI_FG);
    return *value;
}
int gui_textbox(struct gui *g, int x, int y, int w, char *s, size_t cap, int focused) {
    size_t len = strnlen(s, cap);
    int submitted = 0;
    if (!cap || len == cap)
        return 0;
    if (focused && g->event.type == GUI_KEY && g->event.pressed) {
        char c = g->event.character;
        if ((c == '\b' || c == 127) && len)
            s[--len] = 0;
        else if (c == '\n')
            submitted = 1;
        else if (c >= 32 && c < 127 && len + 1 < cap) {
            s[len++] = c;
            s[len] = 0;
        }
    }
    gui_fill(g, x, y, w, 26, 0x0e161f);
    gui_fill(g, x, y + 25, w, 1, focused ? GUI_ACCENT : 0x314657);
    int visible = w > 16 ? (w - 16) / 8 : 0;
    gui_text(g, x + 6, y + 5, s + (len > (size_t)visible ? len - visible : 0), GUI_FG);
    return submitted;
}
void gui_progress(struct gui *g, int x, int y, int w, unsigned v, unsigned total) {
    gui_fill(g, x, y, w, 12, 0x314657);
    if (total)
        gui_fill(g, x, y, (uint64_t)(v > total ? total : v) * w / total, 12, GUI_ACCENT);
}
uint64_t gui_millis(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) < 0)
        return 0;
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
