#include "ps2.h"
#include "hal/hal.h"
#include "console/kprintf.h"

#define QUEUE_SIZE 128
#define WAIT_LIMIT 100000

static struct ps2_status status;
static struct ps2_key_event keys[QUEUE_SIZE];
static struct ps2_mouse_event mice[QUEUE_SIZE];
static unsigned key_head, key_tail, mouse_head, mouse_tail;
static bool extended, released, left_shift, right_shift, caps, caps_down;
static unsigned pause_remaining;
static uint8_t mouse_packet[3], mouse_bytes;
static bool initialized;

static bool wait_input(void) {
    uint64_t deadline = hal_time_ms() + 1000;
    for (unsigned i = 0; i < WAIT_LIMIT * 20u && hal_time_ms() < deadline; i++)
        if (!(hal_io_read8(0x64) & 2))
            return true;
    return false;
}

static bool command(uint8_t value) {
    if (!wait_input())
        return false;
    hal_io_write8(0x64, value);
    return true;
}

static bool data(uint8_t value) {
    if (!wait_input())
        return false;
    hal_io_write8(0x60, value);
    return true;
}

static bool receive(bool auxiliary, uint8_t *value) {
    uint64_t deadline = hal_time_ms() + 2000;
    for (unsigned i = 0; i < WAIT_LIMIT * 40u && hal_time_ms() < deadline; i++) {
        uint8_t flags = hal_io_read8(0x64);
        if (!(flags & 1))
            continue;
        uint8_t byte = hal_io_read8(0x60);
        if (flags & 0xc0) {
            status.errors++;
            continue;
        }
        if (((flags & 0x20) != 0) != auxiliary) {
            status.errors++;
            continue;
        }
        *value = byte;
        return true;
    }
    return false;
}

static bool device_command(bool auxiliary, uint8_t value) {
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        if (auxiliary && !command(0xd4))
            return false;
        uint8_t response;
        if (!data(value) || !receive(auxiliary, &response))
            return false;
        if (response == 0xfa)
            return true;
        if (response != 0xfe)
            return false;
    }
    return false;
}

static bool config_read(uint8_t *value) {
    return command(0x20) && receive(false, value);
}

static bool config_write(uint8_t value) {
    return command(0x60) && data(value);
}

static char key_character(uint8_t code, bool shift) {
    static const char normal[128] = {
        [0x1c] = 'a',  [0x32] = 'b',  [0x21] = 'c',  [0x23] = 'd', [0x24] = 'e', [0x2b] = 'f',
        [0x34] = 'g',  [0x33] = 'h',  [0x43] = 'i',  [0x3b] = 'j', [0x42] = 'k', [0x4b] = 'l',
        [0x3a] = 'm',  [0x31] = 'n',  [0x44] = 'o',  [0x4d] = 'p', [0x15] = 'q', [0x2d] = 'r',
        [0x1b] = 's',  [0x2c] = 't',  [0x3c] = 'u',  [0x2a] = 'v', [0x1d] = 'w', [0x22] = 'x',
        [0x35] = 'y',  [0x1a] = 'z',  [0x16] = '1',  [0x1e] = '2', [0x26] = '3', [0x25] = '4',
        [0x2e] = '5',  [0x36] = '6',  [0x3d] = '7',  [0x3e] = '8', [0x46] = '9', [0x45] = '0',
        [0x0e] = '`',  [0x4e] = '-',  [0x55] = '=',  [0x54] = '[', [0x5b] = ']', [0x5d] = '\\',
        [0x4c] = ';',  [0x52] = '\'', [0x41] = ',',  [0x49] = '.', [0x4a] = '/', [0x29] = ' ',
        [0x5a] = '\n', [0x66] = '\b', [0x0d] = '\t', [0x76] = 27};
    static const char shifted[128] = {
        [0x16] = '!', [0x1e] = '@', [0x26] = '#', [0x25] = '$', [0x2e] = '%', [0x36] = '^',
        [0x3d] = '&', [0x3e] = '*', [0x46] = '(', [0x45] = ')', [0x0e] = '~', [0x4e] = '_',
        [0x55] = '+', [0x54] = '{', [0x5b] = '}', [0x5d] = '|', [0x4c] = ':', [0x52] = '"',
        [0x41] = '<', [0x49] = '>', [0x4a] = '?'};
    if (code >= 128)
        return 0;
    char c = normal[code];
    if (c >= 'a' && c <= 'z')
        return shift != caps ? c - 'a' + 'A' : c;
    return shift && shifted[code] ? shifted[code] : c;
}

static void enqueue_key(uint16_t code, bool pressed, char character) {
    unsigned next = (key_head + 1) % QUEUE_SIZE;
    if (next == key_tail) {
        status.dropped++;
        return;
    }
    keys[key_head] = (struct ps2_key_event){code, pressed, character};
    key_head = next;
}

static void keyboard_byte(uint8_t byte) {
    if (pause_remaining) {
        if (--pause_remaining == 0) {
            enqueue_key(0x200, true, 0);
            enqueue_key(0x200, false, 0);
        }
        return;
    }
    if (byte == 0xe1) {
        pause_remaining = 7;
        extended = released = false;
        return;
    }
    if (byte == 0xe0) {
        extended = true;
        return;
    }
    if (byte == 0xf0) {
        released = true;
        return;
    }
    if (byte == 0xfa || byte == 0xfe)
        return;
    if (byte == 0 || byte == 0xff) {
        status.errors++;
        extended = released = left_shift = right_shift = caps_down = false;
        return;
    }
    if (extended && byte == 0x12) {
        extended = released = false;
        return;
    }
    uint16_t code = byte | (extended ? 0x100 : 0);
    bool down = !released;
    if (!extended) {
        if (byte == 0x12)
            left_shift = down;
        if (byte == 0x59)
            right_shift = down;
        if (byte == 0x58) {
            if (down && !caps_down)
                caps = !caps;
            caps_down = down;
        }
    }
    char c = down && !extended ? key_character(byte, left_shift || right_shift) : 0;
    if (down && extended && byte == 0x5a)
        c = '\n';
    if (down && extended && byte == 0x4a)
        c = '/';
    enqueue_key(code, down, c);
    extended = released = false;
}

static void mouse_byte(uint8_t byte) {
    if (!mouse_bytes && !(byte & 8)) {
        status.errors++;
        return;
    }
    mouse_packet[mouse_bytes++] = byte;
    if (mouse_bytes < 3)
        return;
    mouse_bytes = 0;
    if (mouse_packet[0] & 0xc0) {
        status.errors++;
        return;
    }
    unsigned next = (mouse_head + 1) % QUEUE_SIZE;
    if (next == mouse_tail) {
        status.dropped++;
        return;
    }
    int16_t dx = (int16_t)mouse_packet[1] - ((mouse_packet[0] & 0x10) ? 256 : 0);
    int16_t dy = (int16_t)mouse_packet[2] - ((mouse_packet[0] & 0x20) ? 256 : 0);
    mice[mouse_head] = (struct ps2_mouse_event){dx, -dy, mouse_packet[0] & 7};
    mouse_head = next;
}

static bool ps2_interrupt(void *context) {
    (void)context;
    for (unsigned i = 0; i < 32; i++) {
        uint8_t flags = hal_io_read8(0x64);
        if (!(flags & 1))
            break;
        uint8_t byte = hal_io_read8(0x60);
        if (flags & 0xc0) {
            status.errors++;
            continue;
        }
        if (flags & 0x20) {
            if (status.mouse)
                mouse_byte(byte);
        } else if (status.keyboard)
            keyboard_byte(byte);
    }
    return true;
}

static void flush(void) {
    for (unsigned i = 0; i < 256 && (hal_io_read8(0x64) & 1); i++)
        (void)hal_io_read8(0x60);
}

struct ps2_status ps2_init(void) {
    if (initialized)
        return ps2_get_status();
    initialized = true;
    uint64_t state = hal_irq_save();
    uint8_t config, response;
    if (!command(0xad) || !command(0xa7))
        goto failure;
    flush();
    if (!config_read(&config))
        goto failure;
    config = (config & ~0x43u) | 0x30;
    if (!config_write(config) || !command(0xaa) || !receive(false, &response) || response != 0x55)
        goto failure;
    if (!command(0xad) || !command(0xa7) || !config_write(config))
        goto failure;
    bool second = false;
    if (command(0xa8) && config_read(&response))
        second = !(response & 0x20);
    if (!command(0xa7))
        goto failure;
    bool first_ok = command(0xab) && receive(false, &response) && response == 0;
    bool second_ok = second && command(0xa9) && receive(false, &response) && response == 0;
    if (first_ok && command(0xae)) {
        bool reset = device_command(false, 0xff) && receive(false, &response) && response == 0xaa;
        if (reset && device_command(false, 0xf5) && device_command(false, 0xf0) &&
            device_command(false, 2) && device_command(false, 0xf4)) {
            status.keyboard = hal_irq_register_isa(1, ps2_interrupt, NULL);
        }
    }
    if (!status.keyboard)
        (void)command(0xad);
    if (second_ok && command(0xa8)) {
        bool reset = device_command(true, 0xff) && receive(true, &response) && response == 0xaa;
        reset = reset && receive(true, &response) && response == 0;
        if (reset && device_command(true, 0xf6) && device_command(true, 0xf4))
            status.mouse = hal_irq_register_isa(12, ps2_interrupt, NULL);
    }
    if (!status.mouse)
        (void)command(0xa7);
    config = (config & ~0x73u) | (status.keyboard ? 1 : 0x10) | (status.mouse ? 2 : 0x20);
    if (!config_write(config))
        goto failure;
    if (status.keyboard && !hal_irq_mask_isa(1, false))
        goto failure;
    if (status.mouse && !hal_irq_mask_isa(12, false))
        goto failure;
    kprintf("[ps2] keyboard=%s set=2 mouse=%s packet=3 IRQ1/IRQ12 via IOAPIC\n",
            status.keyboard ? "ready" : "absent/failed", status.mouse ? "ready" : "absent/failed");
    hal_irq_restore(state);
    return status;

failure:
    if (status.keyboard)
        hal_irq_unregister_isa(1, ps2_interrupt, NULL);
    if (status.mouse)
        hal_irq_unregister_isa(12, ps2_interrupt, NULL);
    status.keyboard = status.mouse = false;
    status.errors++;
    (void)command(0xad);
    (void)command(0xa7);
    kprintf("[ps2] controller initialization failed\n");
    hal_irq_restore(state);
    return status;
}

struct ps2_status ps2_get_status(void) {
    uint64_t state = hal_irq_save();
    struct ps2_status copy = status;
    hal_irq_restore(state);
    return copy;
}

bool ps2_read_key(struct ps2_key_event *event) {
    if (!event)
        return false;
    uint64_t state = hal_irq_save();
    bool available = key_tail != key_head;
    if (available) {
        *event = keys[key_tail];
        key_tail = (key_tail + 1) % QUEUE_SIZE;
    }
    hal_irq_restore(state);
    return available;
}

bool ps2_read_mouse(struct ps2_mouse_event *event) {
    if (!event)
        return false;
    uint64_t state = hal_irq_save();
    bool available = mouse_tail != mouse_head;
    if (available) {
        *event = mice[mouse_tail];
        mouse_tail = (mouse_tail + 1) % QUEUE_SIZE;
    }
    hal_irq_restore(state);
    return available;
}
