#include "gui.h"
#define BW 28
#define BH 16
static int xs[BW * BH], ys[BW * BH], length, dx, dy, nx, ny, foodx, foody, over, score;
static void food(void) {
    if (length == BW * BH) {
        over = 2;
        return;
    }
    for (;;) {
        foodx = rand() % BW;
        foody = rand() % BH;
        int occupied = 0;
        for (int i = 0; i < length; i++)
            if (xs[i] == foodx && ys[i] == foody)
                occupied = 1;
        if (!occupied)
            return;
    }
}
static void reset(void) {
    length = 4;
    dx = nx = 1;
    dy = ny = 0;
    over = score = 0;
    for (int i = 0; i < length; i++) {
        xs[i] = 8 - i;
        ys[i] = 8;
    }
    food();
}
static void tick(void) {
    dx = nx;
    dy = ny;
    int x = xs[0] + dx, y = ys[0] + dy, eats = x == foodx && y == foody;
    if (x < 0 || y < 0 || x >= BW || y >= BH) {
        over = 1;
        return;
    }
    for (int i = 0; i < length - !eats; i++)
        if (xs[i] == x && ys[i] == y) {
            over = 1;
            return;
        }
    if (eats) {
        length++;
        score += 10;
    }
    for (int i = length - 1; i > 0; i--) {
        xs[i] = xs[i - 1];
        ys[i] = ys[i - 1];
    }
    xs[0] = x;
    ys[0] = y;
    if (eats)
        food();
}
int main(void) {
    struct gui g;
    if (gui_open(&g) < 0) {
        perror("snake");
        return 1;
    }
    srand(gui_millis());
    reset();
    int quit = 0, paused = 0;
    uint64_t last = gui_millis();
    while (!quit) {
        struct gui_event e;
        do {
            int got = gui_next(&g, &e);
            if (got && e.type == GUI_CLOSE)
                quit = 1;
            if (got && e.type == GUI_KEY && e.pressed) {
                if ((e.code == 0x175 || e.character == 'w') && dy != 1) {
                    nx = 0;
                    ny = -1;
                }
                if ((e.code == 0x172 || e.character == 's') && dy != -1) {
                    nx = 0;
                    ny = 1;
                }
                if ((e.code == 0x16b || e.character == 'a') && dx != 1) {
                    nx = -1;
                    ny = 0;
                }
                if ((e.code == 0x174 || e.character == 'd') && dx != -1) {
                    nx = 1;
                    ny = 0;
                }
                if (e.character == ' ')
                    paused = !paused;
            }
            if (got && e.type == GUI_FOCUS && !e.pressed)
                paused = 1;
            gui_clear(&g, GUI_BG);
            char label[80];
            snprintf(label, sizeof(label), "Snake  Score %d  %s", score,
                     over == 2 ? "YOU WIN"
                     : over    ? "GAME OVER"
                     : paused  ? "PAUSED"
                               : "");
            gui_text(&g, 12, 8, label, GUI_FG);
            gui_fill(&g, 16, 34, BW * 16, BH * 16, 0x0c1720);
            gui_fill(&g, 17 + foodx * 16, 35 + foody * 16, 14, 14, 0xef7667);
            for (int i = 0; i < length; i++)
                gui_fill(&g, 17 + xs[i] * 16, 35 + ys[i] * 16, 14, 14, i ? GUI_ACCENT : 0xb5efd0);
            if (gui_button(&g, 12, 292, 110, "Restart")) {
                reset();
                paused = 0;
            }
            gui_text(&g, 138, 298, "Arrows/WASD  Space: pause", GUI_FG);
            gui_present(&g);
            if (!got)
                break;
        } while (!quit);
        uint64_t now = gui_millis();
        if (now - last >= (uint64_t)(150 - (score < 500 ? score / 5 : 100))) {
            if (!over && !paused)
                tick();
            last = now;
        }
        usleep(15000);
    }
    gui_close(&g);
    return 0;
}
