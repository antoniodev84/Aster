#include "gui.h"
#define COLS 57
#define ROWS 14
static char cells[ROWS][COLS + 1];
static int cx, cy, escape, params[4], param;
static int input = -1, output = -1;
static pid_t shell;
static char line[512], pending[514], message[100];
static size_t pending_at, pending_size;
static void clear_screen(void) {
    for (int y = 0; y < ROWS; y++) {
        memset(cells[y], ' ', COLS);
        cells[y][COLS] = 0;
    }
    cx = cy = 0;
}
static void newline(void) {
    cx = 0;
    if (++cy == ROWS) {
        memmove(cells, cells + 1, sizeof(cells[0]) * (ROWS - 1));
        memset(cells[ROWS - 1], ' ', COLS);
        cells[ROWS - 1][COLS] = 0;
        cy--;
    }
}
static void put(int c) {
    if (escape == 1) {
        escape = c == '[' ? 2 : 0;
        param = 0;
        memset(params, 0, sizeof(params));
        return;
    }
    if (escape == 2) {
        if (c >= '0' && c <= '9') {
            if (params[param] < 1000)
                params[param] = params[param] * 10 + c - '0';
            return;
        }
        if (c == ';') {
            if (param < 3)
                param++;
            return;
        }
        int n = params[0] ? params[0] : 1;
        if (c == 'J' && (params[0] == 2 || params[0] == 3))
            clear_screen();
        else if (c == 'K')
            memset(cells[cy] + cx, ' ', COLS - cx);
        else if (c == 'H' || c == 'f') {
            cy = params[0] ? params[0] - 1 : 0;
            cx = params[1] ? params[1] - 1 : 0;
        } else if (c == 'A')
            cy -= n;
        else if (c == 'B')
            cy += n;
        else if (c == 'C')
            cx += n;
        else if (c == 'D')
            cx -= n;
        if (cx < 0)
            cx = 0;
        if (cy < 0)
            cy = 0;
        if (cx >= COLS)
            cx = COLS - 1;
        if (cy >= ROWS)
            cy = ROWS - 1;
        escape = 0;
        return;
    }
    if (c == 27) {
        escape = 1;
        return;
    }
    if (c == '\n') {
        newline();
        return;
    }
    if (c == '\r') {
        cx = 0;
        return;
    }
    if (c == '\b') {
        if (cx)
            cx--;
        return;
    }
    if (c == '\t') {
        int spaces = 8 - cx % 8;
        while (spaces--)
            put(' ');
        return;
    }
    if (c < 32 || c > 126)
        return;
    cells[cy][cx++] = c;
    if (cx == COLS)
        newline();
}
static void terminate_tree(pid_t root) {
    if (root <= 1 || kill(root, SIGSTOP) < 0)
        return;
    DIR *d = opendir("/proc");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            int pid = atoi(e->d_name);
            if (pid <= 1 || pid == root)
                continue;
            char path[128], text[512];
            snprintf(path, sizeof(path), "/proc/%d/status", pid);
            int fd = open(path, O_RDONLY);
            if (fd < 0)
                continue;
            ssize_t n = read(fd, text, sizeof(text) - 1);
            close(fd);
            if (n <= 0)
                continue;
            text[n] = 0;
            char *ppid = strstr(text, "PPid:\t");
            if (ppid && atoi(ppid + 6) == root)
                terminate_tree(pid);
        }
        closedir(d);
    }
    kill(root, SIGKILL);
}
static int start_shell(void) {
    int in[2], out[2];
    if (pipe(in) < 0)
        return -1;
    if (pipe(out) < 0) {
        close(in[0]);
        close(in[1]);
        return -1;
    }
    shell = fork();
    if (shell < 0) {
        for (int i = 0; i < 2; i++) {
            close(in[i]);
            close(out[i]);
        }
        return -1;
    }
    if (!shell) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(out[1], 2);
        for (int i = 3; i < 128; i++)
            close(i);
        execl("/bin/sh", "sh", (char *)NULL);
        perror("terminal: sh");
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    input = in[1];
    output = out[0];
    fcntl(input, F_SETFL, O_NONBLOCK);
    fcntl(output, F_SETFL, O_NONBLOCK);
    fcntl(input, F_SETFD, FD_CLOEXEC);
    fcntl(output, F_SETFD, FD_CLOEXEC);
    return 0;
}
int main(void) {
    struct gui g;
    if (gui_open(&g) < 0) {
        perror("terminal");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    clear_screen();
    const char *welcome = "Aster shell - commands run in /bin/sh\n";
    for (const char *s = welcome; *s; s++)
        put(*s);
    if (start_shell() < 0) {
        perror("terminal: pipe/fork");
        gui_close(&g);
        return 1;
    }
    int quit = 0;
    while (!quit) {
        char data[1024];
        ssize_t n;
        while (output >= 0 && (n = read(output, data, sizeof(data))) > 0)
            for (ssize_t i = 0; i < n; i++)
                put((unsigned char)data[i]);
        if (pending_at < pending_size) {
            n = write(input, pending + pending_at, pending_size - pending_at);
            if (n > 0)
                pending_at += n;
            else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                snprintf(message, sizeof(message), "%s", strerror(errno));
                pending_at = pending_size;
            }
        }
        if (shell > 0 && waitpid(shell, NULL, WNOHANG) == shell) {
            shell = 0;
            snprintf(message, sizeof(message), "Shell exited. Use Restart.");
        }
        struct gui_event e;
        do {
            int got = gui_next(&g, &e);
            if (got && e.type == GUI_CLOSE)
                quit = 1;
            gui_clear(&g, 0x0e161f);
            for (int y = 0; y < ROWS; y++)
                gui_text(&g, 10, 8 + y * 16, cells[y], GUI_FG);
            gui_fill(&g, 10 + cx * 8, 22 + cy * 16, 7, 2, GUI_ACCENT);
            int send = gui_textbox(&g, 8, 242, 464, line, sizeof(line),
                                   shell > 0 && pending_at == pending_size);
            if (send && shell > 0 && pending_at == pending_size) {
                pending_size = strlen(line);
                memcpy(pending, line, pending_size);
                pending[pending_size++] = '\n';
                pending_at = 0;
                put('$');
                put(' ');
                for (char *s = line; *s; s++)
                    put(*s);
                put('\n');
                line[0] = 0;
            }
            if (gui_button(&g, 8, 282, 100, "Clear"))
                clear_screen();
            if (gui_button(&g, 116, 282, 100, "Restart")) {
                if (shell > 0) {
                    terminate_tree(shell);
                    while (waitpid(shell, NULL, 0) < 0 && errno == EINTR)
                        ;
                }
                close(input);
                close(output);
                pending_at = pending_size = 0;
                line[0] = message[0] = 0;
                if (start_shell() < 0)
                    snprintf(message, sizeof(message), "%s", strerror(errno));
            }
            gui_text(&g, 224, 288, message, GUI_ACCENT);
            gui_present(&g);
            if (!got)
                break;
        } while (!quit);
        usleep(20000);
    }
    close(input);
    close(output);
    if (shell > 0) {
        terminate_tree(shell);
        while (waitpid(shell, NULL, 0) < 0 && errno == EINTR)
            ;
    }
    gui_close(&g);
    return 0;
}
