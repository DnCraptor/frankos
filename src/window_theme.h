/*
 * FRANK OS
 * Copyright (c) 2025 Mikhail Matveev <xtreme@rh1.tech>
 * https://rh1.tech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef WINDOW_THEME_H
#define WINDOW_THEME_H

#include "window.h"

/*==========================================================================
 * Theme IDs
 *=========================================================================*/

#define THEME_ID_WIN95    0
#define THEME_ID_SIMPLE   1
#define THEME_COUNT       2

/*==========================================================================
 * Theme style flags (controls decoration drawing behavior)
 *=========================================================================*/

#define TSTYLE_BEVEL_3D     (1u << 0)  /* Win95-style 3D bevels */
#define TSTYLE_FLAT_BORDER  (1u << 1)  /* Simple 1px border */
#define TSTYLE_DOT_BUTTONS  (1u << 2)  /* macOS-style colored dot buttons */

/*==========================================================================
 * Theme structure — runtime-configurable colors and metrics
 *=========================================================================*/

typedef struct {
    const char *name;

    /* Style flags */
    uint16_t style;

    /* Decoration metrics (pixels) */
    int16_t  title_height;
    int16_t  border_width;
    int16_t  menu_height;
    int16_t  button_w;
    int16_t  button_h;
    int16_t  button_pad;
    int16_t  min_w;
    int16_t  min_h;
    int16_t  resize_grab;

    /* Color scheme */
    uint8_t  desktop_color;

    /* Active window */
    uint8_t  active_title_bg;
    uint8_t  active_title_fg;
    uint8_t  active_border;

    /* Inactive window */
    uint8_t  inactive_title_bg;
    uint8_t  inactive_title_fg;
    uint8_t  inactive_border;

    /* 3D bevel for buttons and frames */
    uint8_t  bevel_light;
    uint8_t  bevel_dark;
    uint8_t  button_face;

    /* Client area default */
    uint8_t  client_bg;

    /* Dot button colors (for TSTYLE_DOT_BUTTONS) */
    uint8_t  dot_close;       /* red */
    uint8_t  dot_minimize;    /* yellow */
    uint8_t  dot_maximize;    /* green */
} theme_t;

/*==========================================================================
 * Global theme pointer — defined in theme.c
 *=========================================================================*/

extern const theme_t *current_theme;

/* Built-in theme table */
extern const theme_t builtin_themes[THEME_COUNT];

/* Set theme by ID (0-based index). Forces full repaint. */
void theme_set(uint8_t theme_id);

/* Get current theme ID */
uint8_t theme_get_id(void);

/*==========================================================================
 * Backward-compatible macros — read from current_theme at runtime
 *=========================================================================*/

#define THEME_TITLE_HEIGHT   (current_theme->title_height)
#define THEME_BORDER_WIDTH   (current_theme->border_width)
#define THEME_MENU_HEIGHT    (current_theme->menu_height)
#define THEME_BUTTON_W       (current_theme->button_w)
#define THEME_BUTTON_H       (current_theme->button_h)
#define THEME_BUTTON_PAD     (current_theme->button_pad)
#define THEME_MIN_W          (current_theme->min_w)
#define THEME_MIN_H          (current_theme->min_h)
#define THEME_RESIZE_GRAB    (current_theme->resize_grab)

#define THEME_DESKTOP_COLOR       (current_theme->desktop_color)
#define THEME_ACTIVE_TITLE_BG     (current_theme->active_title_bg)
#define THEME_ACTIVE_TITLE_FG     (current_theme->active_title_fg)
#define THEME_ACTIVE_BORDER       (current_theme->active_border)
#define THEME_INACTIVE_TITLE_BG   (current_theme->inactive_title_bg)
#define THEME_INACTIVE_TITLE_FG   (current_theme->inactive_title_fg)
#define THEME_INACTIVE_BORDER     (current_theme->inactive_border)
#define THEME_BEVEL_LIGHT         (current_theme->bevel_light)
#define THEME_BEVEL_DARK          (current_theme->bevel_dark)
#define THEME_BUTTON_FACE         (current_theme->button_face)
#define THEME_CLIENT_BG           (current_theme->client_bg)

/*==========================================================================
 * Hit-test zones (returned by theme_hit_test)
 *=========================================================================*/

#define HT_NOWHERE    0
#define HT_CLIENT     1
#define HT_TITLEBAR   2
#define HT_CLOSE      3
#define HT_MAXIMIZE   4
#define HT_MINIMIZE   5
#define HT_BORDER_L   6
#define HT_BORDER_R   7
#define HT_BORDER_T   8
#define HT_BORDER_B   9
#define HT_BORDER_TL 10
#define HT_BORDER_TR 11
#define HT_BORDER_BL 12
#define HT_BORDER_BR 13
#define HT_MENUBAR   14

/*==========================================================================
 * Inline geometry helpers
 *=========================================================================*/

/* Compute the client-area rect in client coordinates (origin 0,0)
 * given the outer frame rect and window flags. */
static inline rect_t theme_client_rect(const rect_t *frame, uint16_t flags) {
    rect_t r;
    r.x = 0;
    r.y = 0;
    r.w = frame->w - 2 * THEME_BORDER_WIDTH;
    r.h = frame->h - THEME_TITLE_HEIGHT - 2 * THEME_BORDER_WIDTH;
    if (flags & WF_MENUBAR) r.h -= THEME_MENU_HEIGHT;
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}

/* Client area origin in screen coordinates */
static inline point_t theme_client_origin(const rect_t *frame, uint16_t flags) {
    point_t p;
    p.x = frame->x + THEME_BORDER_WIDTH;
    p.y = frame->y + THEME_BORDER_WIDTH + THEME_TITLE_HEIGHT;
    if (flags & WF_MENUBAR) p.y += THEME_MENU_HEIGHT;
    return p;
}

/* Close button rect in screen coordinates */
static inline rect_t theme_close_btn_rect(const rect_t *frame) {
    rect_t r;
    r.w = THEME_BUTTON_W;
    r.h = THEME_BUTTON_H;
    r.x = frame->x + frame->w - THEME_BORDER_WIDTH - THEME_BUTTON_W -
          THEME_BUTTON_PAD;
    r.y = frame->y + THEME_BORDER_WIDTH + (THEME_TITLE_HEIGHT - THEME_BUTTON_H) / 2;
    return r;
}

/* Maximize button rect in screen coordinates */
static inline rect_t theme_max_btn_rect(const rect_t *frame) {
    rect_t r;
    r.w = THEME_BUTTON_W;
    r.h = THEME_BUTTON_H;
    r.x = frame->x + frame->w - THEME_BORDER_WIDTH -
          2 * (THEME_BUTTON_W + THEME_BUTTON_PAD);
    r.y = frame->y + THEME_BORDER_WIDTH + (THEME_TITLE_HEIGHT - THEME_BUTTON_H) / 2;
    return r;
}

/* Minimize button rect in screen coordinates */
static inline rect_t theme_min_btn_rect(const rect_t *frame) {
    rect_t r;
    r.w = THEME_BUTTON_W;
    r.h = THEME_BUTTON_H;
    r.x = frame->x + frame->w - THEME_BORDER_WIDTH -
          3 * (THEME_BUTTON_W + THEME_BUTTON_PAD);
    r.y = frame->y + THEME_BORDER_WIDTH + (THEME_TITLE_HEIGHT - THEME_BUTTON_H) / 2;
    return r;
}

/* Hit-test: given a screen-coordinate point and a window's outer frame,
 * return which zone the point falls in. */
static inline uint8_t theme_hit_test(const rect_t *frame, uint16_t flags,
                                      int16_t px, int16_t py) {
    /* Outside the frame entirely? */
    if (px < frame->x || px >= frame->x + frame->w ||
        py < frame->y || py >= frame->y + frame->h) {
        return HT_NOWHERE;
    }

    int16_t lx = px - frame->x;   /* local x within frame */
    int16_t ly = py - frame->y;   /* local y within frame */

    if (!(flags & WF_BORDER)) {
        return HT_CLIENT;
    }

    /* Corner resize zones — wider grab area, checked first so corners
     * take priority over the title bar and client area. */
    if (flags & WF_RESIZABLE) {
        int16_t cg = THEME_RESIZE_GRAB;
        bool cl = lx < cg;
        bool cr = lx >= frame->w - cg;
        bool ct = ly < cg;
        bool cb = ly >= frame->h - cg;

        if (ct && cl) return HT_BORDER_TL;
        if (ct && cr) return HT_BORDER_TR;
        if (cb && cl) return HT_BORDER_BL;
        if (cb && cr) return HT_BORDER_BR;
    }

    /* Title bar area (between top border and client) */
    if (ly >= THEME_BORDER_WIDTH &&
        ly < THEME_BORDER_WIDTH + THEME_TITLE_HEIGHT &&
        lx >= THEME_BORDER_WIDTH &&
        lx < frame->w - THEME_BORDER_WIDTH) {

        /* Check buttons (right side of title bar) */
        if (flags & WF_CLOSABLE) {
            rect_t cb = theme_close_btn_rect(frame);
            if (px >= cb.x && px < cb.x + cb.w &&
                py >= cb.y && py < cb.y + cb.h) {
                return HT_CLOSE;
            }
        }

        if (flags & WF_CLOSABLE) {
            rect_t mb = theme_max_btn_rect(frame);
            if (px >= mb.x && px < mb.x + mb.w &&
                py >= mb.y && py < mb.y + mb.h) {
                return HT_MAXIMIZE;
            }

            rect_t nb = theme_min_btn_rect(frame);
            if (px >= nb.x && px < nb.x + nb.w &&
                py >= nb.y && py < nb.y + nb.h) {
                return HT_MINIMIZE;
            }
        }

        return HT_TITLEBAR;
    }

    /* Menu bar zone (between title bar and client) */
    if (flags & WF_MENUBAR) {
        int16_t menu_top = THEME_BORDER_WIDTH + THEME_TITLE_HEIGHT;
        if (ly >= menu_top && ly < menu_top + THEME_MENU_HEIGHT &&
            lx >= THEME_BORDER_WIDTH &&
            lx < frame->w - THEME_BORDER_WIDTH) {
            return HT_MENUBAR;
        }
    }

    /* Client area */
    {
        int16_t client_top = THEME_BORDER_WIDTH + THEME_TITLE_HEIGHT;
        if (flags & WF_MENUBAR) client_top += THEME_MENU_HEIGHT;
        if (lx >= THEME_BORDER_WIDTH &&
            lx < frame->w - THEME_BORDER_WIDTH &&
            ly >= client_top &&
            ly < frame->h - THEME_BORDER_WIDTH) {
            return HT_CLIENT;
        }
    }

    /* Edge border zones */
    bool left   = lx < THEME_BORDER_WIDTH;
    bool right  = lx >= frame->w - THEME_BORDER_WIDTH;
    bool top    = ly < THEME_BORDER_WIDTH;
    bool bottom = ly >= frame->h - THEME_BORDER_WIDTH;

    if (left)   return HT_BORDER_L;
    if (right)  return HT_BORDER_R;
    if (top)    return HT_BORDER_T;
    if (bottom) return HT_BORDER_B;

    return HT_NOWHERE;
}

#endif /* WINDOW_THEME_H */
