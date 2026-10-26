#include "gui.h"
static const uint16_t shapes[7] = {0x00f0, 0x0066, 0x0072, 0x0071, 0x0074, 0x0036, 0x0063};
static const uint32_t colors[8] = {0x0c1720, 0x42cad4, 0xeace61, 0xa785d5,
                                   0x5b8ed9, 0xe99c5c, 0x70c894, 0xe57480};
static uint8_t board[20][10];
static int kind, next, px, py, rotation, score, lines, over, paused;
static uint64_t fall;
static int cell(int k, int r, int x, int y) {
    for (int i = 0; i < r; i++) {
        int old = x;
        x = y;
        y = 3 - old;
    }
    return (shapes[k] >> (y * 4 + x)) & 1;
}
static int fits(int x, int y, int r) {
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
            if (cell(kind, r, i, j)) {
                int bx = x + i, by = y + j;
                if (bx < 0 || bx >= 10 || by >= 20 || (by >= 0 && board[by][bx]))
                    return 0;
            }
    return 1;
}
static void spawn(void) {
    kind = next;
    next = rand() % 7;
    px = 3;
    py = -1;
    rotation = 0;
    fall = gui_millis();
    if (!fits(px, py, rotation))
        over = 1;
}
static void reset(void) {
    memset(board, 0, sizeof(board));
    score = lines = over = paused = 0;
    next = rand() % 7;
    spawn();
}
static void lock_piece(void) {
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++)
            if (cell(kind, rotation, i, j)) {
                if (py + j < 0) {
                    over = 1;
                    return;
                }
                board[py + j][px + i] = kind + 1;
            }
    int removed = 0;
    for (int y = 19; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < 10; x++)
            if (!board[y][x])
                full = 0;
        if (full) {
            for (int j = y; j > 0; j--)
                memcpy(board[j], board[j - 1], 10);
            memset(board[0], 0, 10);
            removed++;
            y++;
        }
    }
    static const int reward[5] = {0, 100, 300, 500, 800};
    score += reward[removed] * (1 + lines / 10);
    lines += removed;
    spawn();
}
static void drop(void) {
    if (fits(px, py + 1, rotation))
        py++;
    else
        lock_piece();
    fall = gui_millis();
}
static void rotate(void) {
    int r = (rotation + 1) % 4;
    if (kind == 1)
        return;
    static const int kicks[] = {0, -1, 1, -2, 2};
    for (unsigned i = 0; i < sizeof(kicks) / sizeof(kicks[0]); i++)
        if (fits(px + kicks[i], py, r)) {
            px += kicks[i];
            rotation = r;
            return;
        }
    if (fits(px, py - 1, r)) {
        py--;
        rotation = r;
    }
}
static void block(struct gui *g, int x, int y, int k) {
    gui_fill(g, x, y, 11, 11, colors[k]);
    if (k)
        gui_fill(g, x, y, 11, 2, 0xc6dde0);
}
int main(void) {
    struct gui g;
    if (gui_open(&g) < 0) {
        perror("tetris");
        return 1;
    }
    srand(gui_millis());
    reset();
    int quit = 0;
    int held = 0;
    uint64_t repeat = 0;
    while (!quit) {
        struct gui_event e;
        do {
            int got = gui_next(&g, &e);
            if (got && e.type == GUI_CLOSE)
                quit = 1;
            if (got && e.type == GUI_FOCUS && !e.pressed) {
                paused = 1;
                held = 0;
            }
            if (got && e.type == GUI_KEY) {
                if (e.pressed && (e.character == 'p' || e.character == 'P'))
                    paused = !paused;
                if (!e.pressed && e.code == held)
                    held = 0;
                if (e.pressed && !over && !paused) {
                    if (e.code == 0x16b || e.code == 0x174 || e.code == 0x172) {
                        held = e.code;
                        repeat = gui_millis() + 180;
                        int step = e.code == 0x16b ? -1 : 1;
                        if (e.code == 0x172) {
                            drop();
                            score++;
                        } else if (fits(px + step, py, rotation))
                            px += step;
                    }
                    if (e.code == 0x175 || e.character == 'w')
                        rotate();
                    if (e.character == ' ') {
                        while (fits(px, py + 1, rotation)) {
                            py++;
                            score += 2;
                        }
                        lock_piece();
                    }
                }
            }
            gui_clear(&g, GUI_BG);
            gui_text(&g, 24, 12, "Tetris", GUI_ACCENT);
            gui_fill(&g, 22, 38, 124, 244, 0x40535f);
            for (int y = 0; y < 20; y++)
                for (int x = 0; x < 10; x++)
                    block(&g, 24 + x * 12, 40 + y * 12, board[y][x]);
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++)
                    if (cell(kind, rotation, x, y) && py + y >= 0)
                        block(&g, 24 + (px + x) * 12, 40 + (py + y) * 12, kind + 1);
            char text[150];
            snprintf(text, sizeof(text), "Score: %d\nLines: %d\nLevel: %d\n\n%s", score, lines,
                     1 + lines / 10,
                     over     ? "GAME OVER"
                     : paused ? "PAUSED"
                              : "Next:");
            gui_text(&g, 180, 40, text, GUI_FG);
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++)
                    if (cell(next, 0, x, y))
                        block(&g, 190 + x * 12, 140 + y * 12, next + 1);
            gui_text(&g, 180, 204,
                     "Left/Right: move\nUp: rotate\nDown: soft drop\nSpace: hard drop\nP: pause",
                     GUI_FG);
            if (gui_button(&g, 24, 290, 120, "Restart")) {
                reset();
                held = 0;
            }
            if (gui_button(&g, 180, 290, 140, paused ? "Resume" : "Pause"))
                paused = !paused;
            gui_present(&g);
            if (!got)
                break;
        } while (!quit);
        uint64_t now = gui_millis();
        if (!over && !paused) {
            if (held && now >= repeat) {
                int step = held == 0x16b ? -1 : 1;
                if (held == 0x172) {
                    drop();
                    score++;
                } else if (fits(px + step, py, rotation))
                    px += step;
                repeat = now + 65;
            }
            int speed = 650 - (lines / 10) * 55;
            if (speed < 100)
                speed = 100;
            if (now - fall >= (uint64_t)speed)
                drop();
        }
        usleep(15000);
    }
    gui_close(&g);
    return 0;
}
