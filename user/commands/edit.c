#include "common.h"

static struct termios saved_terminal;
static char *text;
static size_t length, capacity, cursor, top_line, left_column;
static unsigned rows = 25, columns = 80;
static bool modified, raw_enabled;
static const char *filename;
static char message[256];

static void restore_terminal(void) {
    if (raw_enabled) {
        tcsetattr(0, TCSAFLUSH, &saved_terminal);
        output_all(1, "\033[0m\033[?25h\033[2J\033[H", 17);
        raw_enabled = false;
    }
}
static void terminate(int signal) {
    restore_terminal();
    _exit(128 + signal);
}
static bool reserve(size_t size) {
    if (size <= capacity)
        return true;
    size_t next = capacity ? capacity * 2 : 4096;
    if (next < size)
        next = size;
    char *p = realloc(text, next);
    if (!p)
        return false;
    text = p;
    capacity = next;
    return true;
}
static size_t line_start(size_t position) {
    while (position && text[position - 1] != '\n')
        position--;
    return position;
}
static size_t line_end(size_t position) {
    while (position < length && text[position] != '\n')
        position++;
    return position;
}
static size_t line_number(size_t position) {
    size_t n = 0;
    for (size_t i = 0; i < position; i++)
        if (text[i] == '\n')
            n++;
    return n;
}
static size_t visual_column(size_t position) {
    size_t n = 0;
    for (size_t i = line_start(position); i < position; i++)
        n = text[i] == '\t' ? (n + 4) & ~3ul : n + 1;
    return n;
}
static size_t position_column(size_t start, size_t wanted) {
    size_t col = 0;
    while (start < length && text[start] != '\n') {
        size_t next = text[start] == '\t' ? (col + 4) & ~3ul : col + 1;
        if (next > wanted)
            break;
        col = next;
        start++;
    }
    return start;
}
static void move_vertical(int direction) {
    size_t start = line_start(cursor), col = visual_column(cursor);
    if (direction < 0) {
        if (!start)
            return;
        start = line_start(start - 1);
    } else {
        start = line_end(start);
        if (start == length)
            return;
        start++;
    }
    cursor = position_column(start, col);
}
static void refresh(void) {
    size_t current_line = line_number(cursor), current_column = visual_column(cursor);
    if (current_line < top_line)
        top_line = current_line;
    if (current_line >= top_line + rows - 2)
        top_line = current_line - (rows - 3);
    if (current_column < left_column)
        left_column = current_column;
    if (current_column >= left_column + columns)
        left_column = current_column - columns + 1;
    printf("\033[?25l\033[H\033[7m %-.*s%s\033[K\033[0m", (int)(columns > 15 ? columns - 15 : 1),
           filename, modified ? " [modified]" : "");
    size_t position = 0;
    for (size_t i = 0; i < top_line && position < length; i++) {
        position = line_end(position);
        if (position < length)
            position++;
    }
    for (unsigned screen_row = 1; screen_row < rows - 1; screen_row++) {
        printf("\033[%u;1H", screen_row + 1);
        size_t end = line_end(position), visual = 0;
        unsigned drawn = 0;
        for (size_t i = position; i < end; i++) {
            unsigned cells = text[i] == '\t' ? 4 - visual % 4 : 1;
            for (unsigned j = 0; j < cells; j++, visual++) {
                if (visual < left_column || drawn >= columns)
                    continue;
                bool here = i == cursor && j == 0;
                if (here)
                    fputs("\033[7m", stdout);
                putchar(text[i] == '\t' ? ' ' : isprint((unsigned char)text[i]) ? text[i] : '?');
                if (here)
                    fputs("\033[0m", stdout);
                drawn++;
            }
        }
        if (cursor == end && position <= length && current_line == top_line + screen_row - 1 &&
            current_column >= left_column && current_column - left_column < columns) {
            printf("\033[7m \033[0m");
            drawn++;
        }
        fputs("\033[K", stdout);
        if (end < length)
            position = end + 1;
        else
            position = length;
    }
    printf("\033[%u;1H\033[7m", rows);
    char status[512];
    if (message[0])
        snprintf(status, sizeof(status), " %s", message);
    else
        snprintf(status, sizeof(status),
                 " Ctrl-S Save | Ctrl-Q Quit | Line %zu Col %zu | %zu bytes", current_line + 1,
                 current_column + 1, length);
    printf("%-.*s\033[K\033[0m", (int)columns, status);
    printf("\033[%zu;%zuH\033[?25h", current_line - top_line + 2, current_column - left_column + 1);
    fflush(stdout);
}
static int key(void) {
    unsigned char c;
    ssize_t n;
    do {
        n = read(0, &c, 1);
    } while (n < 0 && errno == EINTR);
    if (n != 1)
        return -1;
    if (c != 27)
        return c;
    unsigned char a, b;
    if (read(0, &a, 1) != 1)
        return 27;
    if (a != '[' && a != 'O')
        return a;
    if (read(0, &b, 1) != 1)
        return 27;
    if (b >= '0' && b <= '9') {
        unsigned char end;
        if (read(0, &end, 1) != 1)
            return 27;
        if (end == '~') {
            if (b == '3')
                return 1005;
            if (b == '5')
                return 1008;
            if (b == '6')
                return 1009;
            if (b == '1' || b == '7')
                return 1006;
            if (b == '4' || b == '8')
                return 1007;
        }
        return 27;
    }
    switch (b) {
    case 'A':
        return 1001;
    case 'B':
        return 1002;
    case 'C':
        return 1003;
    case 'D':
        return 1004;
    case 'H':
        return 1006;
    case 'F':
        return 1007;
    default:
        return 27;
    }
}
static void save(void) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        snprintf(message, sizeof(message), "Save failed: %s", strerror(errno));
        return;
    }
    if (output_all(fd, text, length) < 0 || fsync(fd) < 0) {
        snprintf(message, sizeof(message), "Save failed: %s", strerror(errno));
        close(fd);
        return;
    }
    close(fd);
    modified = false;
    snprintf(message, sizeof(message), "Saved %zu bytes to %s", length, filename);
}
int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: edit FILE\n", stderr);
        return 2;
    }
    filename = argv[1];
    text = load_file(filename, &length);
    if (!text) {
        if (errno != ENOENT) {
            perror(filename);
            return 1;
        }
        length = 0;
        text = malloc(4096);
        capacity = 4096;
        if (!text)
            return 1;
    } else
        capacity = length + 1;
    if (tcgetattr(0, &saved_terminal) < 0) {
        perror("edit: terminal");
        free(text);
        return 1;
    }
    struct winsize size;
    if (ioctl(0, TIOCGWINSZ, &size) == 0) {
        if (size.ws_row >= 5)
            rows = size.ws_row;
        if (size.ws_col >= 20)
            columns = size.ws_col;
    }
    struct termios raw = saved_terminal;
    cfmakeraw(&raw);
    if (tcsetattr(0, TCSAFLUSH, &raw) < 0) {
        perror("edit: terminal");
        free(text);
        return 1;
    }
    raw_enabled = true;
    atexit(restore_terminal);
    signal(SIGTERM, terminate);
    signal(SIGHUP, terminate);
    signal(SIGINT, terminate);
    int quit_count = 0;
    for (;;) {
        refresh();
        int c = key();
        if (c < 0)
            break;
        message[0] = 0;
        if (c == 17) {
            if (!modified || quit_count) {
                break;
            }
            snprintf(message, sizeof(message),
                     "Unsaved changes. Ctrl-S saves; press Ctrl-Q again to discard.");
            quit_count = 1;
            continue;
        }
        quit_count = 0;
        if (c == 19) {
            save();
            continue;
        }
        if (c == 1001)
            move_vertical(-1);
        else if (c == 1002)
            move_vertical(1);
        else if (c == 1003) {
            if (cursor < length)
                cursor++;
        } else if (c == 1004) {
            if (cursor)
                cursor--;
        } else if (c == 1006 || c == 1)
            cursor = line_start(cursor);
        else if (c == 1007 || c == 5)
            cursor = line_end(cursor);
        else if (c == 1008)
            for (unsigned i = 0; i < rows - 2; i++)
                move_vertical(-1);
        else if (c == 1009)
            for (unsigned i = 0; i < rows - 2; i++)
                move_vertical(1);
        else if (c == 127 || c == 8) {
            if (cursor) {
                memmove(text + cursor - 1, text + cursor, length - cursor);
                cursor--;
                length--;
                modified = true;
            }
        } else if (c == 1005) {
            if (cursor < length) {
                memmove(text + cursor, text + cursor + 1, length - cursor - 1);
                length--;
                modified = true;
            }
        } else if (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c < 127)) {
            if (!reserve(length + 2)) {
                snprintf(message, sizeof(message), "Out of memory");
                continue;
            }
            memmove(text + cursor + 1, text + cursor, length - cursor);
            text[cursor++] = c == '\r' ? '\n' : c;
            length++;
            modified = true;
        }
    }
    restore_terminal();
    free(text);
    return 0;
}
