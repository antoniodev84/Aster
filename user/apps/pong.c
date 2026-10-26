#include "gui.h"
static int bx, by, vx, vy, player, ai, leftscore, rightscore, paused, up, down;
static void serve(int direction) {
    bx = 240;
    by = 150;
    vx = 4 * direction;
    vy = (rand() % 2 ? 3 : -3);
}
static void reset(void) {
    player = ai = 140;
    leftscore = rightscore = paused = 0;
    up = down = 0;
    serve(1);
}
int main(void) {
    struct gui g;
    if (gui_open(&g) < 0) {
        perror("pong");
        return 1;
    }
    srand(gui_millis());
    reset();
    int quit = 0;
    uint64_t last = gui_millis();
    while (!quit) {
        struct gui_event e;
        do {
            int got = gui_next(&g, &e);
            if (got && e.type == GUI_CLOSE)
                quit = 1;
            if (got && e.type == GUI_MOUSE && e.y >= 30 && e.y <= 270)
                player = e.y;
            if (got && e.type == GUI_KEY) {
                if (e.code == 0x175 || e.code == 0x1d)
                    up = e.pressed;
                if (e.code == 0x172 || e.code == 0x1b)
                    down = e.pressed;
                if (e.pressed && e.character == ' ')
                    paused = !paused;
            }
            if (got && e.type == GUI_FOCUS && !e.pressed) {
                paused = 1;
                up = down = 0;
            }
            gui_clear(&g, GUI_BG);
            char text[100];
            snprintf(text, sizeof(text), "Pong   %d : %d   %s", leftscore, rightscore,
                     leftscore >= 11    ? "YOU WIN"
                     : rightscore >= 11 ? "GAME OVER"
                     : paused           ? "PAUSED"
                                        : "First to 11");
            gui_text(&g, 12, 8, text, GUI_FG);
            gui_fill(&g, 10, 32, 460, 248, 0x0c1720);
            for (int y = 40; y < 275; y += 20)
                gui_fill(&g, 239, y, 2, 10, 0x314657);
            gui_fill(&g, 24, player - 24, 8, 48, GUI_ACCENT);
            gui_fill(&g, 448, ai - 24, 8, 48, 0xef7667);
            gui_fill(&g, bx - 4, by - 4, 8, 8, GUI_FG);
            if (gui_button(&g, 12, 290, 110, "Restart"))
                reset();
            gui_text(&g, 132, 296, "Mouse/W/S  Space: pause", GUI_FG);
            gui_present(&g);
            if (!got)
                break;
        } while (!quit);
        uint64_t now = gui_millis();
        if (now - last >= 20) {
            last = now;
            if (!paused && leftscore < 11 && rightscore < 11) {
                player += (down - up) * 6;
                if (player < 58)
                    player = 58;
                if (player > 254)
                    player = 254;
                if (by > ai + 5)
                    ai += 3;
                if (by < ai - 5)
                    ai -= 3;
                if (ai < 58)
                    ai = 58;
                if (ai > 254)
                    ai = 254;
                bx += vx;
                by += vy;
                if (by <= 36) {
                    by = 36;
                    vy = abs(vy);
                }
                if (by >= 276) {
                    by = 276;
                    vy = -abs(vy);
                }
                if (vx < 0 && bx <= 36 && bx >= 22 && abs(by - player) <= 28) {
                    bx = 36;
                    vx = abs(vx) + (abs(vx) < 9);
                    vy = (by - player) / 6;
                    if (!vy)
                        vy = 1;
                }
                if (vx > 0 && bx >= 444 && bx <= 458 && abs(by - ai) <= 28) {
                    bx = 444;
                    vx = -abs(vx) - (abs(vx) < 9);
                    vy = (by - ai) / 6;
                    if (!vy)
                        vy = -1;
                }
                if (bx < 10) {
                    rightscore++;
                    serve(1);
                }
                if (bx > 470) {
                    leftscore++;
                    serve(-1);
                }
            }
        }
        usleep(10000);
    }
    gui_close(&g);
    return 0;
}
