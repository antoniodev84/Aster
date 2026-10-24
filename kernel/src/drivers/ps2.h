#ifndef PS2_H
#define PS2_H

#include <stdbool.h>
#include <stdint.h>

struct ps2_key_event {
    uint16_t code;
    bool pressed;
    char character;
};

struct ps2_mouse_event {
    int16_t dx, dy;
    uint8_t buttons;
};

struct ps2_status {
    bool keyboard, mouse;
    uint64_t dropped, errors;
};

struct ps2_status ps2_init(void);
struct ps2_status ps2_get_status(void);
bool ps2_read_key(struct ps2_key_event *event);
bool ps2_read_mouse(struct ps2_mouse_event *event);

#endif
