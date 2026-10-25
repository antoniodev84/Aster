#include "tty.h"
#include "ps2.h"
#include "hal/hal.h"
#include "console/kprintf.h"
#include "console/framebuffer.h"
#include "proc/process.h"
#include <abi.h>
#include "memory.h"

static struct abi_termios attributes;
static uint8_t input[4096], line[4096];
static size_t head, tail, used, line_length;
static bool eof_pending, control, ready;
static int foreground = 1;
static struct abi_winsize winsize;

void tty_init(void) {
    attributes = (struct abi_termios){.c_iflag = ICRNL,
                                      .c_oflag = OPOST | ONLCR,
                                      .c_cflag = 0xbf,
                                      .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK};
    attributes.c_cc[VINTR] = 3;
    attributes.c_cc[VQUIT] = 28;
    attributes.c_cc[VERASE] = 127;
    attributes.c_cc[VKILL] = 21;
    attributes.c_cc[VEOF] = 4;
    attributes.c_cc[VSUSP] = 26;
    attributes.c_cc[VMIN] = 1;
    size_t columns, rows;
    framebuffer_console_dimensions(&columns, &rows);
    winsize = (struct abi_winsize){.ws_row = rows ? rows : 25, .ws_col = columns ? columns : 80};
    ready = true;
}
long tty_write(const void *buffer, size_t count) {
    const char *bytes = buffer;
    for (size_t i = 0; i < count; i++)
        kprintf("%c", bytes[i]);
    return count;
}
static void push(uint8_t byte) {
    if (used < sizeof(input)) {
        input[head] = byte;
        head = (head + 1) % sizeof(input);
        used++;
    }
}
static void echo(uint8_t c) {
    if (attributes.c_lflag & ECHO) {
        if (c < 32 && c != '\n' && c != '\t') {
            char s[2] = {'^', (char)(c + '@')};
            tty_write(s, 2);
        } else
            tty_write(&c, 1);
    }
}
static void receive(uint8_t c) {
    if (c >= 128) {
        return; 
    }

    if ((attributes.c_iflag & ICRNL) && c == '\r')
        c = '\n';

    if (attributes.c_lflag & ISIG) {
        int signal = c == attributes.c_cc[VINTR]   ? SIGINT
                     : c == attributes.c_cc[VQUIT] ? SIGQUIT
                     : c == attributes.c_cc[VSUSP] ? SIGTSTP
                                                   : 0;
        if (signal) {
            echo(c);
            tty_write("\n", 1);
            line_length = 0;
            for (struct process *p = process_list(); p; p = p->next)
                if (p->pgrp == foreground)
                    process_signal(p, signal);
            return;
        }
    }
    if (attributes.c_lflag & ICANON) {
        if (c == attributes.c_cc[VERASE] || c == '\b') {
            if (line_length) {
                line_length--;
                if (attributes.c_lflag & ECHOE)
                    tty_write("\b \b", 3);
            }
            return;
        }
        if (c == attributes.c_cc[VKILL]) {
            while (line_length) {
                line_length--;
                if (attributes.c_lflag & ECHOE)
                    tty_write("\b \b", 3);
            }
            return;
        }
        if (c == attributes.c_cc[VEOF]) {
            if (!line_length)
                eof_pending = true;
            else {
                for (size_t i = 0; i < line_length; i++)
                    push(line[i]);
                line_length = 0;
            }
            process_wake_io();
            return;
        }
        if (line_length < sizeof(line) - 1) {
            line[line_length++] = c;
            echo(c);
        }
        if (c == '\n') {
            for (size_t i = 0; i < line_length; i++)
                push(line[i]);
            line_length = 0;
            process_wake_io();
        }
    } else {
        push(c);
        echo(c);
        process_wake_io();
    }
}
void tty_poll(void) {
    if (!ready)
        return;
    for (unsigned i = 0; i < 256 && (hal_io_read8(0x3fd) & 1); i++)
        receive(hal_io_read8(0x3f8));
    struct ps2_key_event key;
    while (ps2_read_key(&key)) {
        if (key.code == 0x14 || key.code == 0x114) {
            control = key.pressed;
            continue;
        }
        if (!key.pressed)
            continue;
        if (key.code == 0x175) {
            const char *s = "\033[A";
            while (*s)
                receive(*s++);
        } else if (key.code == 0x172) {
            const char *s = "\033[B";
            while (*s)
                receive(*s++);
        } else if (key.code == 0x174) {
            const char *s = "\033[C";
            while (*s)
                receive(*s++);
        } else if (key.code == 0x16b) {
            const char *s = "\033[D";
            while (*s)
                receive(*s++);
        } else if (key.code == 0x171) {
            const char *s = "\033[3~";
            while (*s)
                receive(*s++);
        } else if (key.character) {
            uint8_t c = key.character;
            if (control && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
                c = (c | 32) - 'a' + 1;
            if (c == '\b')
                c = 127;
            receive(c);
        }
    }
}
long tty_read(void *buffer, size_t count) {
    struct process *p = process_current();
    if (p && p->pgrp != foreground) {
        process_signal(p, SIGTTIN);
        return -EINTR;

    }
    if (!count)
        return 0;
    if (!used) {
        if (eof_pending) {
            eof_pending = false;
            return 0;
        }
        if (!(attributes.c_lflag & ICANON) && !attributes.c_cc[VMIN])
            return 0;
        return -EAGAIN;
    }
    size_t done = 0;
    while (done < count && used) {
        uint8_t c = input[tail];
        tail = (tail + 1) % sizeof(input);
        used--;
        ((uint8_t *)buffer)[done++] = c;
        if ((attributes.c_lflag & ICANON) && c == '\n')
            break;
    }
    return done;
}
long tty_ioctl(unsigned long request, void *argument) {
    switch (request) {
    case TCGETS:
        memcpy(argument, &attributes, sizeof(attributes));
        return 0;
    case TCSETS:
    case TCSETSW:
    case TCSETSF:
        memcpy(&attributes, argument, sizeof(attributes));
        if (request == TCSETSF)
            head = tail = used = line_length = 0;
        return 0;
    case TIOCGWINSZ:
        memcpy(argument, &winsize, sizeof(winsize));
        return 0;
    case TIOCSWINSZ:
        memcpy(&winsize, argument, sizeof(winsize));
        for (struct process *p = process_list(); p; p = p->next)
            if (p->pgrp == foreground)
                process_signal(p, SIGWINCH);
        return 0;
    case TIOCGPGRP:
        *(int *)argument = foreground;
        return 0;
    case TIOCSPGRP: {
        int group = *(int *)argument;
        struct process *self = process_current();
        bool exists = false;
        for (struct process *p = process_list(); p; p = p->next)
            if (p->pgrp == group && (!self || p->sid == self->sid))
                exists = true;
        if (!exists)
            return -EPERM;
        foreground = group;
        return 0;
    }
    case FIONREAD:
        *(int *)argument = used;
        return 0;
    default:
        return -ENOTTY;
    }
}
