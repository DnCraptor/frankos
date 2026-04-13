/*
 * FRANK OS — Network Settings Internal App
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://rh1.tech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NETWORK_SETTINGS_H
#define NETWORK_SETTINGS_H

#include "window.h"

/* Create and show the Network Settings window.
 * Returns the window handle, or HWND_NULL on failure. */
hwnd_t network_settings_create(void);

#endif /* NETWORK_SETTINGS_H */
