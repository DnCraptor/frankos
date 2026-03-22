/*
 * FRANK OS — Theme system
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://rh1.tech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "window_theme.h"
#include "window.h"

/*==========================================================================
 * Built-in themes
 *=========================================================================*/

const theme_t builtin_themes[THEME_COUNT] = {
    /* THEME_ID_WIN95 — classic Windows 95 */
    {
        .name             = "Windows 95",
        .style            = TSTYLE_BEVEL_3D,

        .title_height     = 20,
        .border_width     = 4,
        .menu_height      = 20,
        .button_w         = 16,
        .button_h         = 14,
        .button_pad       = 2,
        .min_w            = 80,
        .min_h            = 40,
        .resize_grab      = 6,

        .desktop_color    = COLOR_CYAN,

        .active_title_bg  = COLOR_BLUE,
        .active_title_fg  = COLOR_WHITE,
        .active_border    = COLOR_LIGHT_GRAY,

        .inactive_title_bg = COLOR_DARK_GRAY,
        .inactive_title_fg = COLOR_LIGHT_GRAY,
        .inactive_border   = COLOR_DARK_GRAY,

        .bevel_light      = COLOR_WHITE,
        .bevel_dark       = COLOR_DARK_GRAY,
        .button_face      = COLOR_LIGHT_GRAY,

        .client_bg        = COLOR_WHITE,

        .dot_close        = COLOR_RED,
        .dot_minimize     = COLOR_YELLOW,
        .dot_maximize     = COLOR_GREEN,
    },

    /* THEME_ID_SIMPLE — flat with macOS-style colored dots */
    {
        .name             = "Simple",
        .style            = TSTYLE_FLAT_BORDER | TSTYLE_DOT_BUTTONS,

        .title_height     = 20,
        .border_width     = 4,
        .menu_height      = 20,
        .button_w         = 16,
        .button_h         = 14,
        .button_pad       = 2,
        .min_w            = 80,
        .min_h            = 40,
        .resize_grab      = 6,

        .desktop_color    = COLOR_CYAN,

        .active_title_bg  = COLOR_LIGHT_GRAY,
        .active_title_fg  = COLOR_BLACK,
        .active_border    = COLOR_LIGHT_GRAY,

        .inactive_title_bg = COLOR_LIGHT_GRAY,
        .inactive_title_fg = COLOR_DARK_GRAY,
        .inactive_border   = COLOR_LIGHT_GRAY,

        .bevel_light      = COLOR_LIGHT_GRAY,
        .bevel_dark       = COLOR_DARK_GRAY,
        .button_face      = COLOR_LIGHT_GRAY,

        .client_bg        = COLOR_WHITE,

        .dot_close        = COLOR_RED,
        .dot_minimize     = COLOR_YELLOW,
        .dot_maximize     = COLOR_GREEN,
    },
};

/*==========================================================================
 * Current theme state
 *=========================================================================*/

static uint8_t current_theme_id = THEME_ID_WIN95;
const theme_t *current_theme = &builtin_themes[THEME_ID_WIN95];

void theme_set(uint8_t theme_id) {
    if (theme_id >= THEME_COUNT) theme_id = THEME_ID_WIN95;
    current_theme_id = theme_id;
    current_theme = &builtin_themes[theme_id];
    wm_force_full_repaint();
}

uint8_t theme_get_id(void) {
    return current_theme_id;
}
