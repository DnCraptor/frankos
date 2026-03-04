/*
 * FRANK OS — Control Panel
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://rh1.tech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CONTROL_PANEL_H
#define CONTROL_PANEL_H

#include "window.h"

/* Create and show the Control Panel main window.
 * Returns the window handle, or HWND_NULL on failure. */
hwnd_t control_panel_create(void);

/* 16x16 and 32x32 icons for the Control Panel window */
const uint8_t *cp_get_icon16(void);
const uint8_t *cp_get_icon32(void);

#endif /* CONTROL_PANEL_H */
