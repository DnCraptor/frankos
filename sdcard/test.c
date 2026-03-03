// test.c — minimal event debug test
// Usage: cc test.c
// Every event increments a counter shown on screen.

#include <frankos>

int hwnd;
int count;

void paint_handler(int hw) {
    char buf[32];
    wd_begin(hw);
    wd_clear(COLOR_LIGHT_GRAY);
    sprintf(buf, "Events: %d", count);
    wd_text_ui(10, 10, buf, COLOR_BLACK, COLOR_LIGHT_GRAY);
    wd_text_ui(10, 30, "Click or move mouse here", COLOR_DARK_GRAY, COLOR_LIGHT_GRAY);
    wd_text_ui(10, 50, "Counter should increase", COLOR_DARK_GRAY, COLOR_LIGHT_GRAY);
    wd_end();
}

void event_handler(int hw, char *ev) {
    int type;
    type = ev[0] & 255;

    if (type == WM_CLOSE) {
        wm_destroy_window(hw);
        return;
    }

    // Any event: increment counter and repaint
    count = count + 1;
    wm_invalidate(hw);
}

int main() {
    count = 0;
    hwnd = wm_create_window(
        100, 100, 300, 200,
        "Event Test",
        WSTYLE_DEFAULT,
        event_handler,
        paint_handler
    );

    if (hwnd == HWND_NULL) {
        printf("Failed to create window!\n");
        return 1;
    }

    wm_show_window(hwnd);
    wm_set_focus(hwnd);
    printf("Window created. Move mouse inside to test events.\n");

    while (wm_get_window(hwnd))
        sleep_ms(100);

    printf("Window closed. Total events: %d\n", count);
    return 0;
}
