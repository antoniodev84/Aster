#ifndef ASTER_GUI_H
#define ASTER_GUI_H
#include <os.h>
#define GUI_MAGIC 0x41535452u
#define GUI_WIDTH 480
#define GUI_HEIGHT 320
#define GUI_EVENTS 128
#define GUI_BG 0x19232e
#define GUI_FG 0xe4edf5
#define GUI_ACCENT 0x35aeb0
#define GUI_MOUSE 1
#define GUI_KEY 2
#define GUI_CLOSE 3
#define GUI_FOCUS 4
struct gui_event {
    int type, x, y, buttons, code, pressed, character;
};
struct gui_shared {
    uint32_t magic, width, height, sequence, head, tail;
    struct gui_event events[GUI_EVENTS];
    uint32_t pixels[GUI_WIDTH * GUI_HEIGHT];
};
struct gui {
    int width, height, fd;
    uint32_t *pixels;
    struct gui_shared *shared;
    struct gui_event event;
};
int gui_open(struct gui *g);
void gui_close(struct gui *g);
int gui_next(struct gui *g, struct gui_event *e);
void gui_present(struct gui *g);
void gui_fill(struct gui *g, int x, int y, int w, int h, uint32_t color);
void gui_text(struct gui *g, int x, int y, const char *text, uint32_t color);
void gui_clear(struct gui *g, uint32_t color);
int gui_button(struct gui *g, int x, int y, int w, const char *label);
int gui_checkbox(struct gui *g, int x, int y, const char *label, int *value);
int gui_textbox(struct gui *g, int x, int y, int w, char *text, size_t capacity, int focused);
void gui_progress(struct gui *g, int x, int y, int w, unsigned value, unsigned total);
int gui_hit(struct gui *g, int x, int y, int w, int h);
uint64_t gui_millis(void);
#endif
