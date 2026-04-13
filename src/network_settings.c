/*
 * FRANK OS — Network Settings Internal App
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://rh1.tech
 *
 * WiFi settings window: scan for networks, connect, disconnect.
 * Accessible from Start -> Settings -> Network.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "network_settings.h"
#include "netcard.h"
#include "wifi_config.h"
#include "window.h"
#include "window_event.h"
#include "window_theme.h"
#include "window_draw.h"
#include "controls.h"
#include "font.h"
#include "dialog.h"
#include "taskbar.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include <string.h>
#include <stdio.h>

/* Icons */
extern const uint8_t *net_icon16_connect_get(void);
extern const uint8_t *net_icon32_connect_get(void);

/*==========================================================================
 * Constants
 *=========================================================================*/

#define NS_MAX_NETWORKS    20
#define NS_LIST_Y          34      /* top of network list in client coords */
#define NS_LIST_ROW_H      16      /* height of each list row */
#define NS_VISIBLE_ROWS    10      /* max visible rows */
#define NS_BTN_W           70
#define NS_BTN_H           22
#define NS_BTN_Y           (NS_LIST_Y + NS_VISIBLE_ROWS * NS_LIST_ROW_H + 8)

/*==========================================================================
 * State
 *=========================================================================*/

typedef struct {
    char ssid[33];
    int  rssi;
    int  enc;
    int  ch;
} ns_network_t;

typedef struct {
    hwnd_t  hwnd;
    ns_network_t networks[NS_MAX_NETWORKS];
    int     net_count;
    int8_t  selected;
    int8_t  scroll_top;
    bool    scanning;
    bool    connecting;
    uint8_t conn_dots;          /* animation: 0-3 dots */
    TimerHandle_t conn_timer;   /* 500ms timer for dot animation */
    scrollbar_t vscroll;        /* vertical scrollbar for network list */
} ns_state_t;

/* Heap-allocated to save BSS — allocated on create, freed on close */
static ns_state_t *nsp;
#define ns (*nsp)

/*==========================================================================
 * Scrollbar helpers
 *=========================================================================*/

static void ns_update_scrollbar(void) {
    int list_h = NS_VISIBLE_ROWS * NS_LIST_ROW_H;
    int content_h = ns.net_count * NS_LIST_ROW_H;
    rect_t cr = wm_get_client_rect(ns.hwnd);
    int cw = cr.w;

    ns.vscroll.x = cw - 5 - SCROLLBAR_WIDTH;
    ns.vscroll.y = NS_LIST_Y;
    ns.vscroll.w = SCROLLBAR_WIDTH;
    ns.vscroll.h = list_h;
    scrollbar_set_range(&ns.vscroll, content_h, list_h);
    scrollbar_set_pos(&ns.vscroll, ns.scroll_top * NS_LIST_ROW_H);
}

/*==========================================================================
 * Scan callback (called on netcard task, fills state)
 *=========================================================================*/

static void ns_scan_cb(const char *ssid, int rssi, int enc, int ch) {
    if (!nsp || ns.net_count >= NS_MAX_NETWORKS) return;
    ns_network_t *n = &ns.networks[ns.net_count];
    strncpy(n->ssid, ssid, 32);
    n->ssid[32] = '\0';
    n->rssi = rssi;
    n->enc = enc;
    n->ch = ch;
    ns.net_count++;
}

static void ns_scan_done(bool success) {
    ns.scanning = false;
    if (nsp && ns.hwnd != HWND_NULL) {
        ns_update_scrollbar();
        wm_invalidate(ns.hwnd);
    }
    (void)success;
}

static void ns_conn_timer_stop(void) {
    if (ns.conn_timer) {
        xTimerStop(ns.conn_timer, 0);
        xTimerDelete(ns.conn_timer, 0);
        ns.conn_timer = NULL;
    }
}

static void ns_join_done(bool success) {
    if (!nsp) return;
    ns.connecting = false;
    ns_conn_timer_stop();
    if (ns.hwnd != HWND_NULL)
        wm_invalidate(ns.hwnd);
    if (success) {
        taskbar_invalidate();
    } else {
        /* Show error dialog */
        dialog_show(ns.hwnd, "Network",
                    "Connection failed.\n\n"
                    "Check the password and try again.",
                    DLG_ICON_ERROR, DLG_BTN_OK);
    }
}

static void ns_quit_done(bool success) {
    if (nsp && ns.hwnd != HWND_NULL)
        wm_invalidate(ns.hwnd);
    taskbar_invalidate();
    (void)success;
}

/*==========================================================================
 * Helpers
 *=========================================================================*/

static const char *enc_str(int enc) {
    switch (enc) {
    case 2: return "WPA";
    case 4: return "WPA2";
    case 5: return "WEP";
    case 7: return "Open";
    case 8: return "Auto";
    default: return "?";
    }
}

/* Signal bars: 4=strong, 1=weak */
static int signal_bars(int rssi) {
    if (rssi >= -50) return 4;
    if (rssi >= -60) return 3;
    if (rssi >= -70) return 2;
    return 1;
}

static void ns_start_scan(void) {
    ns.scanning = true;
    ns.net_count = 0;
    ns.selected = 0;
    ns.scroll_top = 0;
    netcard_request_scan(ns_scan_cb, ns_scan_done);
    if (ns.hwnd != HWND_NULL)
        wm_invalidate(ns.hwnd);
}

/* Timer callback: animate dots in status line */
static void ns_conn_timer_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    if (!nsp || !ns.connecting) return;
    ns.conn_dots = (ns.conn_dots + 1) % 4;
    wm_invalidate(ns.hwnd);
}

static void ns_start_connect(const char *ssid, const char *pass) {
    ns.connecting = true;
    ns.conn_dots = 0;
    /* Start dot animation timer (500ms auto-reload) */
    if (!ns.conn_timer) {
        ns.conn_timer = xTimerCreate("nconn", pdMS_TO_TICKS(500),
                                     pdTRUE, NULL, ns_conn_timer_cb);
    }
    if (ns.conn_timer)
        xTimerStart(ns.conn_timer, 0);
    netcard_request_join(ssid, pass, ns_join_done);
    wm_invalidate(ns.hwnd);
}

/* Button geometry helpers */
static int ns_client_w(void) {
    rect_t r = wm_get_client_rect(ns.hwnd);
    return r.w;
}

/*==========================================================================
 * Button hit-test
 *=========================================================================*/

enum { BTN_SCAN = 0, BTN_CONNECT, BTN_DISCONNECT, BTN_CLOSE, BTN_COUNT };

static void ns_btn_rect(int btn, int cw, int *ox, int *oy, int *ow, int *oh) {
    int spacing = 8;
    int total_w = BTN_COUNT * NS_BTN_W + (BTN_COUNT - 1) * spacing;
    int start_x = (cw - total_w) / 2;
    *ox = start_x + btn * (NS_BTN_W + spacing);
    *oy = NS_BTN_Y;
    *ow = NS_BTN_W;
    *oh = NS_BTN_H;
}

static int ns_btn_hit(int16_t x, int16_t y) {
    int cw = ns_client_w();
    for (int i = 0; i < BTN_COUNT; i++) {
        int bx, by, bw, bh;
        ns_btn_rect(i, cw, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh)
            return i;
    }
    return -1;
}

static int ns_list_hit(int16_t x, int16_t y) {
    if (y < NS_LIST_Y || y >= NS_LIST_Y + NS_VISIBLE_ROWS * NS_LIST_ROW_H)
        return -1;
    int row = (y - NS_LIST_Y) / NS_LIST_ROW_H + ns.scroll_top;
    if (row >= ns.net_count)
        return -1;
    (void)x;
    return row;
}

/*==========================================================================
 * Event handler
 *=========================================================================*/

static bool ns_event(hwnd_t hwnd, const window_event_t *ev) {
    /* Let scrollbar handle mouse events first */
    if (ev->type == WM_LBUTTONDOWN || ev->type == WM_LBUTTONUP ||
        ev->type == WM_MOUSEMOVE) {
        int32_t new_pos;
        if (scrollbar_event(&ns.vscroll, ev, &new_pos)) {
            ns.scroll_top = new_pos / NS_LIST_ROW_H;
            if (ns.scroll_top < 0) ns.scroll_top = 0;
            int max_top = ns.net_count - NS_VISIBLE_ROWS;
            if (max_top < 0) max_top = 0;
            if (ns.scroll_top > max_top) ns.scroll_top = max_top;
            scrollbar_set_pos(&ns.vscroll, ns.scroll_top * NS_LIST_ROW_H);
            wm_invalidate(hwnd);
            return true;
        }
    }

    switch (ev->type) {
    case WM_CLOSE: {
        extern void vPortFree(void *);
        ns_conn_timer_stop();
        wm_destroy_window(hwnd);
        vPortFree(nsp);
        nsp = NULL;
        return true;
    }

    case WM_LBUTTONDOWN: {
        int row = ns_list_hit(ev->mouse.x, ev->mouse.y);
        if (row >= 0) {
            ns.selected = row;
            wm_invalidate(hwnd);
        }
        return true;
    }

    case WM_LBUTTONUP: {
        int btn = ns_btn_hit(ev->mouse.x, ev->mouse.y);
        if (btn == BTN_SCAN && !ns.scanning && !ns.connecting) {
            ns_start_scan();
        } else if (btn == BTN_CONNECT && ns.connecting) {
            /* Cancel ongoing connection */
            ns.connecting = false;
            ns_conn_timer_stop();
            netcard_request_quit(NULL);
            wm_invalidate(hwnd);
        } else if (btn == BTN_CONNECT && ns.selected >= 0 && ns.selected < ns.net_count &&
                   !ns.scanning && !ns.connecting) {
            if (ns.networks[ns.selected].enc == 7) {
                /* Open network */
                ns_start_connect(ns.networks[ns.selected].ssid, "");
            } else {
                /* Need password */
                dialog_input_set_mask(true);
                dialog_input_show(hwnd, "WiFi Password",
                                  "Enter password:", NULL, 64);
            }
        } else if (btn == BTN_DISCONNECT && netcard_wifi_connected()) {
            netcard_request_quit(ns_quit_done);
        } else if (btn == BTN_CLOSE) {
            window_event_t close_ev;
            memset(&close_ev, 0, sizeof(close_ev));
            close_ev.type = WM_CLOSE;
            wm_post_event(hwnd, &close_ev);
        }
        return true;
    }

    case WM_COMMAND:
        /* Password dialog result */
        if (ev->command.id == DLG_RESULT_INPUT && ns.selected >= 0) {
            const char *pass = dialog_input_get_text();
            if (pass)
                ns_start_connect(ns.networks[ns.selected].ssid, pass);
        }
        return true;

    case WM_KEYDOWN:
        if (ev->key.scancode == 0x52 /* UP */ && ns.selected > 0) {
            ns.selected--;
            if (ns.selected < ns.scroll_top) {
                ns.scroll_top = ns.selected;
                ns_update_scrollbar();
            }
            wm_invalidate(hwnd);
            return true;
        }
        if (ev->key.scancode == 0x51 /* DOWN */ && ns.selected < ns.net_count - 1) {
            ns.selected++;
            if (ns.selected >= ns.scroll_top + NS_VISIBLE_ROWS) {
                ns.scroll_top = ns.selected - NS_VISIBLE_ROWS + 1;
                ns_update_scrollbar();
            }
            wm_invalidate(hwnd);
            return true;
        }
        break;

    default:
        break;
    }
    return false;
}

/*==========================================================================
 * Paint handler
 *=========================================================================*/

static void ns_draw_button(int btn, int cw, const char *label, bool enabled) {
    int bx, by, bw, bh;
    ns_btn_rect(btn, cw, &bx, &by, &bw, &bh);

    /* Raised button style */
    wd_fill_rect(bx, by, bw, bh, THEME_BUTTON_FACE);
    wd_hline(bx, by, bw, COLOR_WHITE);
    wd_vline(bx, by, bh, COLOR_WHITE);
    wd_hline(bx, by + bh - 1, bw, COLOR_BLACK);
    wd_vline(bx + bw - 1, by, bh, COLOR_BLACK);
    wd_hline(bx + 1, by + bh - 2, bw - 2, COLOR_DARK_GRAY);
    wd_vline(bx + bw - 2, by + 1, bh - 2, COLOR_DARK_GRAY);

    int tw = (int)strlen(label) * FONT_UI_WIDTH;
    int tx = bx + (bw - tw) / 2;
    int ty = by + (bh - FONT_UI_HEIGHT) / 2;
    wd_text_ui(tx, ty, label,
               enabled ? COLOR_BLACK : COLOR_DARK_GRAY,
               THEME_BUTTON_FACE);
}

static void ns_paint(hwnd_t hwnd) {
    wd_begin(hwnd);
    wd_clear(THEME_BUTTON_FACE);

    rect_t cr = wm_get_client_rect(hwnd);
    int cw = cr.w;
    bool has_sb = ns.vscroll.visible;
    int list_content_w = cw - 10 - (has_sb ? SCROLLBAR_WIDTH : 0);

    /* Status line */
    if (ns.connecting) {
        char status[64];
        const char *dots = &"..."[3 - ns.conn_dots]; /* "", ".", "..", "..." */
        if (ns.selected >= 0 && ns.selected < ns.net_count)
            snprintf(status, sizeof(status), "Connecting to %s%s",
                     ns.networks[ns.selected].ssid, dots);
        else
            snprintf(status, sizeof(status), "Connecting%s", dots);
        wd_text_ui(8, 6, status, COLOR_BLACK, THEME_BUTTON_FACE);
    } else if (netcard_wifi_connected()) {
        char status[80];
        snprintf(status, sizeof(status), "Connected to: %s (%s)",
                 wifi_config_get()->ssid, netcard_wifi_ip());
        wd_text_ui(8, 6, status, COLOR_BLACK, THEME_BUTTON_FACE);
    } else if (!netcard_is_available()) {
        wd_text_ui(8, 6, "No network adapter detected", COLOR_DARK_GRAY, THEME_BUTTON_FACE);
    } else {
        wd_text_ui(8, 6, "Not connected", COLOR_BLACK, THEME_BUTTON_FACE);
    }

    /* Separator */
    wd_hline(4, 18, cw - 8, COLOR_DARK_GRAY);
    wd_hline(4, 19, cw - 8, COLOR_WHITE);

    /* Column headers */
    wd_text_ui(8, 22, "Network", COLOR_BLACK, THEME_BUTTON_FACE);
    wd_text_ui(5 + list_content_w - 90, 22, "Signal", COLOR_BLACK, THEME_BUTTON_FACE);
    wd_text_ui(5 + list_content_w - 40, 22, "Type", COLOR_BLACK, THEME_BUTTON_FACE);

    /* Network list (sunken well) */
    int list_h = NS_VISIBLE_ROWS * NS_LIST_ROW_H;
    int well_w = cw - 8;
    wd_fill_rect(4, NS_LIST_Y - 1, well_w, list_h + 2, COLOR_WHITE);
    wd_hline(4, NS_LIST_Y - 1, well_w, COLOR_DARK_GRAY);
    wd_vline(4, NS_LIST_Y - 1, list_h + 2, COLOR_DARK_GRAY);
    wd_hline(5, NS_LIST_Y + list_h, well_w - 1, COLOR_WHITE);
    wd_vline(4 + well_w - 1, NS_LIST_Y, list_h, COLOR_WHITE);

    if (ns.scanning) {
        wd_text_ui(8, NS_LIST_Y + list_h / 2 - FONT_UI_HEIGHT / 2,
                   "Scanning...", COLOR_DARK_GRAY, COLOR_WHITE);
    } else if (ns.net_count == 0) {
        wd_text_ui(8, NS_LIST_Y + list_h / 2 - FONT_UI_HEIGHT / 2,
                   "No networks found. Click Scan.", COLOR_DARK_GRAY, COLOR_WHITE);
    } else {
        for (int i = 0; i < NS_VISIBLE_ROWS && (i + ns.scroll_top) < ns.net_count; i++) {
            int ni = i + ns.scroll_top;
            bool sel = (ni == ns.selected);
            uint8_t fg = sel ? COLOR_WHITE : COLOR_BLACK;
            uint8_t bg = sel ? COLOR_BLUE : COLOR_WHITE;
            int ry = NS_LIST_Y + i * NS_LIST_ROW_H;

            wd_fill_rect(5, ry, list_content_w, NS_LIST_ROW_H, bg);

            /* SSID */
            wd_text_ui(8, ry + (NS_LIST_ROW_H - FONT_UI_HEIGHT) / 2,
                       ns.networks[ni].ssid, fg, bg);

            /* Signal bars */
            int bars = signal_bars(ns.networks[ni].rssi);
            char sig[8];
            for (int b = 0; b < 4; b++)
                sig[b] = (b < bars) ? '|' : ' ';
            sig[4] = '\0';
            wd_text_ui(5 + list_content_w - 90, ry + (NS_LIST_ROW_H - FONT_UI_HEIGHT) / 2,
                       sig, fg, bg);

            /* Encryption type */
            wd_text_ui(5 + list_content_w - 40, ry + (NS_LIST_ROW_H - FONT_UI_HEIGHT) / 2,
                       enc_str(ns.networks[ni].enc), fg, bg);
        }
    }

    /* Scrollbar */
    ns_update_scrollbar();
    if (ns.vscroll.visible)
        scrollbar_paint(&ns.vscroll);

    /* Buttons */
    bool can_act = netcard_is_available() && !ns.scanning && !ns.connecting;
    ns_draw_button(BTN_SCAN, cw, "Scan", can_act);
    if (ns.connecting) {
        ns_draw_button(BTN_CONNECT, cw, "Cancel", true);
    } else {
        ns_draw_button(BTN_CONNECT, cw, "Connect",
                       can_act && ns.selected >= 0 && ns.selected < ns.net_count);
    }
    ns_draw_button(BTN_DISCONNECT, cw, "Disconnect",
                   can_act && netcard_wifi_connected());
    ns_draw_button(BTN_CLOSE, cw, "Close", true);

    wd_end();
}

/*==========================================================================
 * Create
 *=========================================================================*/

hwnd_t network_settings_create(void) {
    extern void *pvPortMalloc(unsigned int);

    if (!netcard_is_available()) {
        dialog_show(HWND_NULL, "Network",
                    "No network adapter detected.\n\n"
                    "Please insert a network adapter\n"
                    "into your FRANK and reboot.",
                    DLG_ICON_ERROR, DLG_BTN_OK);
        return HWND_NULL;
    }

    if (nsp) return nsp->hwnd;  /* already open */
    nsp = (ns_state_t *)pvPortMalloc(sizeof(ns_state_t));
    if (!nsp) return HWND_NULL;
    memset(&ns, 0, sizeof(ns));
    ns.selected = -1;

    scrollbar_init(&ns.vscroll, false);

    int w = 340 + THEME_BORDER_WIDTH * 2;
    int h = NS_BTN_Y + NS_BTN_H + 12 + THEME_TITLE_HEIGHT + THEME_BORDER_WIDTH * 2;

    hwnd_t hwnd = wm_create_window(
        80, 40, w, h,
        "Network", WSTYLE_DIALOG,
        ns_event, ns_paint);

    if (hwnd == HWND_NULL) return HWND_NULL;

    ns.hwnd = hwnd;
    wm_set_focus(hwnd);
    taskbar_invalidate();

    /* Auto-scan on open if modem is available */
    if (netcard_is_available())
        ns_start_scan();

    return hwnd;
}
