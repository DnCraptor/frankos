/*
 * FRANK OS
 * Copyright (c) 2025 Mikhail Matveev <xtreme@rh1.tech>
 * https://rh1.tech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

#include "window.h"

/* Result ID posted as WM_COMMAND to parent when a file is selected */
#define DLG_RESULT_FILE  0xFF20

/* Open a modal file browser dialog.
 * parent       - owner window (receives WM_COMMAND with result)
 * title        - dialog title bar text
 * initial_path - starting directory (e.g., "/fos"), NULL defaults to "/"
 * filter_ext   - extension filter including dot (e.g., ".tap"), NULL = all files
 * Returns dialog hwnd (HWND_NULL on failure). */
hwnd_t file_dialog_open(hwnd_t parent, const char *title,
                        const char *initial_path, const char *filter_ext);

/* Get the full path of the selected file.
 * Only valid after DLG_RESULT_FILE is received by parent. */
const char *file_dialog_get_path(void);

#endif /* FILE_DIALOG_H */
