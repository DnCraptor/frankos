/*
 * FRANK OS — Find/Replace Dialog
 * Copyright (c) 2025 Mikhail Matveev <xtreme@rh1.tech>
 * https://rh1.tech
 *
 * Modeless dialog following the dialog.c input pattern.
 * Two text fields, Match Case checkbox, and action buttons.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "find_dialog.h"
#include "controls.h"
#include "window.h"
#include "window_event.h"
#include "window_draw.h"
#include "window_theme.h"
#include "display.h"
#include "gfx.h"
#include "font.h"
#include "taskbar.h"
#include <string.h>
#include "FreeRTOS.h"
#include "timers.h"

/*==========================================================================
 * Constants
 *=========================================================================*/

#define FND_FIND_BUF_MAX    64
#define FND_REPL_BUF_MAX    64

/* Layout */
#define FND_PAD              8
#define FND_LABEL_W         90
#define FND_FIELD_H         20
#define FND_FIELD_GAP        4
#define FND_BTN_W           90
#define FND_BTN_H           23
#define FND_BTN_GAP          4

/* Find-only client area */
#define FND_FIND_CLIENT_W   360
#define FND_FIND_CLIENT_H   100

/* Replace client area (taller) */
#define FND_REPL_CLIENT_W   360
#define FND_REPL_CLIENT_H   130

/*==========================================================================
 * Static state (single dialog)
 *=========================================================================*/

static hwnd_t   fnd_hwnd   = HWND_NULL;
static hwnd_t   fnd_parent = HWND_NULL;
static bool     fnd_replace_mode;

/* Text fields */
static char     fnd_find_buf[FND_FIND_BUF_MAX + 1];
static char     fnd_repl_buf[FND_REPL_BUF_MAX + 1];
static textfield_t fnd_find_tf;
static textfield_t fnd_repl_tf;

/* Focus: 0 = find field, 1 = replace field, 2+ = buttons */
static int8_t   fnd_focus;
static checkbox_t fnd_checkbox;

/* Cursor blink */
static TimerHandle_t fnd_blink_timer = NULL;

/* Button press state */
static int8_t   fnd_btn_pressed;  /* -1 = none */

/* Cached layout */
static int16_t  fnd_client_w;
static int16_t  fnd_client_h;
static int16_t  fnd_field_x;
static int16_t  fnd_field_w;

/*==========================================================================
 * Blink timer
 *=========================================================================*/

static void fnd_blink_callback(TimerHandle_t xTimer) {
    (void)xTimer;
    if (fnd_focus == 0)
        textfield_blink(&fnd_find_tf);
    else if (fnd_focus == 1)
        textfield_blink(&fnd_repl_tf);
}

static void fnd_blink_reset(void) {
    fnd_find_tf.cursor_visible = true;
    fnd_repl_tf.cursor_visible = true;
    if (fnd_blink_timer) xTimerReset(fnd_blink_timer, 0);
    wm_invalidate(fnd_hwnd);
}

/* Sunken field and checkbox drawing now use controls.h */

/*==========================================================================
 * Layout helpers
 *=========================================================================*/

static int16_t fnd_find_field_y(void) { return FND_PAD; }
static int16_t fnd_repl_field_y(void) { return FND_PAD + FND_FIELD_H + FND_FIELD_GAP; }

static int16_t fnd_checkbox_y(void) {
    if (fnd_replace_mode)
        return fnd_repl_field_y() + FND_FIELD_H + FND_FIELD_GAP + 2;
    else
        return fnd_find_field_y() + FND_FIELD_H + FND_FIELD_GAP + 2;
}

static int16_t fnd_btn_x(void) {
    return fnd_client_w - FND_PAD - FND_BTN_W;
}

/* Button count: Find=2 (Find Next, Cancel), Replace=4 (Find Next, Replace, Replace All, Cancel) */
static int fnd_btn_count(void) {
    return fnd_replace_mode ? 4 : 2;
}

static const char *fnd_btn_label(int idx) {
    if (fnd_replace_mode) {
        switch (idx) {
            case 0: return "Find Next";
            case 1: return "Replace";
            case 2: return "Replace All";
            case 3: return "Cancel";
        }
    } else {
        switch (idx) {
            case 0: return "Find Next";
            case 1: return "Cancel";
        }
    }
    return "";
}

static int16_t fnd_btn_y(int idx) {
    return FND_PAD + idx * (FND_BTN_H + FND_BTN_GAP);
}

/* Map focus index to meaning: 0=find, 1=replace(if mode), then buttons */
static int fnd_first_btn_focus(void) {
    return fnd_replace_mode ? 2 : 1;
}

static int fnd_max_focus(void) {
    return fnd_first_btn_focus() + fnd_btn_count();
}

static bool fnd_is_field_focus(void) {
    return fnd_focus < fnd_first_btn_focus();
}

static int fnd_btn_index(void) {
    if (!fnd_is_field_focus())
        return fnd_focus - fnd_first_btn_focus();
    return -1;
}

/*==========================================================================
 * Paint
 *=========================================================================*/

static void fnd_paint(hwnd_t hwnd) {
    (void)hwnd;

    /* "Find what:" field */
    wd_text_ui(FND_PAD, fnd_find_field_y() + (FND_FIELD_H - FONT_UI_HEIGHT) / 2,
               "Find what:", COLOR_BLACK, THEME_BUTTON_FACE);
    fnd_find_tf.focused = (fnd_focus == 0);
    textfield_paint(&fnd_find_tf);

    /* "Replace with:" field (replace mode only) */
    if (fnd_replace_mode) {
        wd_text_ui(FND_PAD,
                   fnd_repl_field_y() + (FND_FIELD_H - FONT_UI_HEIGHT) / 2,
                   "Replace with:", COLOR_BLACK, THEME_BUTTON_FACE);
        fnd_repl_tf.focused = (fnd_focus == 1);
        textfield_paint(&fnd_repl_tf);
    }

    /* "Match case" checkbox */
    fnd_checkbox.x = FND_PAD;
    fnd_checkbox.y = fnd_checkbox_y();
    checkbox_paint(&fnd_checkbox);

    /* Buttons (right column) */
    int bc = fnd_btn_count();
    int bx = fnd_btn_x();
    int bi = fnd_btn_index();
    for (int i = 0; i < bc; i++) {
        wd_button(bx, fnd_btn_y(i), FND_BTN_W, FND_BTN_H,
                  fnd_btn_label(i),
                  !fnd_is_field_focus() && bi == i,
                  fnd_btn_pressed == i);
    }
}

/*==========================================================================
 * Close
 *=========================================================================*/

static void fnd_do_close(void) {
    if (fnd_blink_timer) {
        xTimerStop(fnd_blink_timer, 0);
        xTimerDelete(fnd_blink_timer, 0);
        fnd_blink_timer = NULL;
    }
    wm_destroy_window(fnd_hwnd);
    fnd_hwnd = HWND_NULL;
    if (fnd_parent != HWND_NULL)
        wm_set_focus(fnd_parent);
}

/*==========================================================================
 * Post result to parent
 *=========================================================================*/

static void fnd_post_result(uint16_t result) {
    if (fnd_parent != HWND_NULL) {
        window_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = WM_COMMAND;
        ev.command.id = result;
        wm_post_event(fnd_parent, &ev);
    }
}

/*==========================================================================
 * Button action dispatch
 *=========================================================================*/

static void fnd_activate_button(int idx) {
    if (fnd_replace_mode) {
        switch (idx) {
            case 0: fnd_post_result(DLG_RESULT_FIND_NEXT); fnd_do_close(); return;
            case 1: fnd_post_result(DLG_RESULT_REPLACE); fnd_do_close(); return;
            case 2: fnd_post_result(DLG_RESULT_REPLACE_ALL); fnd_do_close(); return;
            case 3: fnd_do_close(); return;
        }
    } else {
        switch (idx) {
            case 0: fnd_post_result(DLG_RESULT_FIND_NEXT); fnd_do_close(); return;
            case 1: fnd_do_close(); return;
        }
    }
}

/* Text field editing now uses controls.h textfield_t */

/*==========================================================================
 * Event handler
 *=========================================================================*/

static bool fnd_event(hwnd_t hwnd, const window_event_t *event) {
    (void)hwnd;

    switch (event->type) {
    case WM_CLOSE:
        fnd_do_close();
        return true;

    case WM_CHAR:
        if (fnd_is_field_focus()) {
            textfield_t *tf = (fnd_focus == 0) ? &fnd_find_tf : &fnd_repl_tf;
            tf->focused = true;
            if (textfield_event(tf, event)) {
                fnd_blink_reset();
                return true;
            }
        }
        return false;

    case WM_KEYDOWN: {
        uint8_t sc = event->key.scancode;

        /* Escape always closes */
        if (sc == 0x29) {
            fnd_do_close();
            return true;
        }

        /* Enter — find next or activate focused button */
        if (sc == 0x28) {
            if (fnd_is_field_focus())
                fnd_activate_button(0); /* Find Next */
            else
                fnd_activate_button(fnd_btn_index());
            return true;
        }

        /* Tab — cycle focus */
        if (sc == 0x2B) {
            fnd_focus = (fnd_focus + 1) % fnd_max_focus();
            wm_invalidate(fnd_hwnd);
            return true;
        }

        /* Field editing keys */
        if (fnd_is_field_focus()) {
            textfield_t *tf = (fnd_focus == 0) ? &fnd_find_tf : &fnd_repl_tf;
            tf->focused = true;
            if (textfield_event(tf, event)) {
                fnd_blink_reset();
                return true;
            }
        }
        return false;
    }

    case WM_LBUTTONDOWN: {
        int mx = event->mouse.x;
        int my = event->mouse.y;

        fnd_btn_pressed = -1;

        /* Hit-test: Find field */
        if (textfield_event(&fnd_find_tf, event)) {
            fnd_focus = 0;
            fnd_blink_reset();
            return true;
        }

        /* Hit-test: Replace field */
        if (fnd_replace_mode && textfield_event(&fnd_repl_tf, event)) {
            fnd_focus = 1;
            fnd_blink_reset();
            return true;
        }

        /* Hit-test: Match case checkbox */
        {
            bool changed = false;
            if (checkbox_event(&fnd_checkbox, event, &changed) && changed) {
                wm_invalidate(fnd_hwnd);
                return true;
            }
        }

        /* Hit-test: Buttons */
        {
            int bc = fnd_btn_count();
            int bx = fnd_btn_x();
            for (int i = 0; i < bc; i++) {
                int by = fnd_btn_y(i);
                if (mx >= bx && mx < bx + FND_BTN_W &&
                    my >= by && my < by + FND_BTN_H) {
                    fnd_btn_pressed = i;
                    fnd_focus = fnd_first_btn_focus() + i;
                    wm_invalidate(fnd_hwnd);
                    return true;
                }
            }
        }
        return true;
    }

    case WM_LBUTTONUP: {
        if (fnd_btn_pressed < 0) return true;
        int pressed = fnd_btn_pressed;
        fnd_btn_pressed = -1;

        int bx = fnd_btn_x();
        int by = fnd_btn_y(pressed);
        int mx = event->mouse.x;
        int my = event->mouse.y;

        if (mx >= bx && mx < bx + FND_BTN_W &&
            my >= by && my < by + FND_BTN_H) {
            fnd_activate_button(pressed);
        } else {
            wm_invalidate(fnd_hwnd);
        }
        return true;
    }

    default:
        return false;
    }
}

/*==========================================================================
 * Internal open helper
 *=========================================================================*/

static hwnd_t fnd_open(hwnd_t parent, bool replace_mode) {
    /* If already open, just switch mode and refocus */
    if (fnd_hwnd != HWND_NULL) {
        if (fnd_replace_mode != replace_mode) {
            /* Need to recreate with different size */
            fnd_do_close();
        } else {
            wm_set_focus(fnd_hwnd);
            return fnd_hwnd;
        }
    }

    fnd_parent = parent;
    fnd_replace_mode = replace_mode;
    fnd_focus = 0;
    fnd_btn_pressed = -1;
    {
        bool was_checked = fnd_checkbox.checked;
        checkbox_init(&fnd_checkbox, FND_PAD, 0, "Match case");
        fnd_checkbox.checked = was_checked;
    }

    /* Keep existing search text if any */

    if (replace_mode) {
        fnd_client_w = FND_REPL_CLIENT_W;
        fnd_client_h = FND_REPL_CLIENT_H;
    } else {
        fnd_client_w = FND_FIND_CLIENT_W;
        fnd_client_h = FND_FIND_CLIENT_H;
    }

    fnd_field_x = FND_LABEL_W;
    fnd_field_w = fnd_client_w - FND_LABEL_W - FND_PAD - FND_BTN_W - FND_PAD;

    int outer_w = fnd_client_w + 2 * THEME_BORDER_WIDTH;
    int outer_h = fnd_client_h + THEME_TITLE_HEIGHT + 2 * THEME_BORDER_WIDTH;

    /* Position near top-right of screen */
    int work_h = taskbar_work_area_height();
    int x = display_width - outer_w - 20;
    int y = 40;
    if (x < 0) x = 0;
    if (y + outer_h > work_h) y = work_h - outer_h;
    if (y < 0) y = 0;

    const char *title = replace_mode ? "Replace" : "Find";
    fnd_hwnd = wm_create_window((int16_t)x, (int16_t)y,
                                  (int16_t)outer_w, (int16_t)outer_h,
                                  title, WSTYLE_DIALOG,
                                  fnd_event, fnd_paint);
    if (fnd_hwnd == HWND_NULL) return HWND_NULL;

    /* Initialize text fields (preserve existing text) */
    {
        int16_t find_len = (int16_t)strlen(fnd_find_buf);
        int16_t repl_len = (int16_t)strlen(fnd_repl_buf);
        textfield_init(&fnd_find_tf, fnd_find_buf, FND_FIND_BUF_MAX + 1,
                       fnd_hwnd);
        fnd_find_tf.len = find_len;
        fnd_find_tf.cursor = find_len;
        textfield_set_rect(&fnd_find_tf, fnd_field_x, fnd_find_field_y(),
                           fnd_field_w, FND_FIELD_H);

        textfield_init(&fnd_repl_tf, fnd_repl_buf, FND_REPL_BUF_MAX + 1,
                       fnd_hwnd);
        fnd_repl_tf.len = repl_len;
        fnd_repl_tf.cursor = repl_len;
        textfield_set_rect(&fnd_repl_tf, fnd_field_x, fnd_repl_field_y(),
                           fnd_field_w, FND_FIELD_H);
    }

    window_t *win = wm_get_window(fnd_hwnd);
    if (win) win->bg_color = THEME_BUTTON_FACE;

    wm_set_focus(fnd_hwnd);
    /* Note: NOT modal — user can still interact with parent */

    /* Cursor blink timer */
    fnd_find_tf.cursor_visible = true;
    fnd_repl_tf.cursor_visible = true;
    fnd_blink_timer = xTimerCreate("fndblink", pdMS_TO_TICKS(500),
                                     pdTRUE, NULL, fnd_blink_callback);
    if (fnd_blink_timer) xTimerStart(fnd_blink_timer, 0);

    return fnd_hwnd;
}

/*==========================================================================
 * Public API
 *=========================================================================*/

hwnd_t find_dialog_show(hwnd_t parent) {
    return fnd_open(parent, false);
}

hwnd_t replace_dialog_show(hwnd_t parent) {
    return fnd_open(parent, true);
}

const char *find_dialog_get_text(void) {
    return fnd_find_buf;
}

const char *find_dialog_get_replace_text(void) {
    return fnd_repl_buf;
}

bool find_dialog_case_sensitive(void) {
    return fnd_checkbox.checked;
}

void find_dialog_close(void) {
    if (fnd_hwnd != HWND_NULL)
        fnd_do_close();
}
