#include "gui.h"
int main(void) {
    struct gui g;
    if (gui_open(&g) < 0) {
        perror("clock");
        return 1;
    }
    uint64_t start = gui_millis(), elapsed = 0;
    int stopwatch = 0, counting = 0, quit = 0;
    while (!quit) {
        struct gui_event e;
        do {
            int got = gui_next(&g, &e);
            if (got && e.type == GUI_CLOSE)
                quit = 1;
            gui_clear(&g, GUI_BG);
            gui_text(&g, 24, 24, "Clock", GUI_ACCENT);
            gui_checkbox(&g, 24, 60, "Stopwatch", &stopwatch);
            char text[80];
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            uint64_t seconds =
                stopwatch ? (elapsed + (counting ? gui_millis() - start : 0)) / 1000 : now.tv_sec;
            snprintf(text, sizeof(text), "%02llu : %02llu : %02llu",
                     (unsigned long long)(seconds / 3600 % 24),
                     (unsigned long long)(seconds / 60 % 60), (unsigned long long)(seconds % 60));
            gui_text(&g, 144, 120, text, GUI_FG);
            if (stopwatch) {
                if (gui_button(&g, 80, 180, 140, counting ? "Pause" : "Start")) {
                    if (counting)
                        elapsed += gui_millis() - start;
                    else
                        start = gui_millis();
                    counting = !counting;
                }
                if (gui_button(&g, 240, 180, 140, "Reset")) {
                    elapsed = 0;
                    start = gui_millis();
                }
            } else
                gui_text(&g, 144, 180, "System time (UTC)", GUI_FG);
            snprintf(text, sizeof(text), "Uptime: %llu seconds",
                     (unsigned long long)(gui_millis() / 1000));
            gui_text(&g, 80, 270, text, 0x92a9ba);
            gui_present(&g);
            if (!got)
                break;
        } while (!quit);
        usleep(50000);
    }
    gui_close(&g);
    return 0;
}
