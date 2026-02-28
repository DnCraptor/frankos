/*
 * frankos_main.c - FRANK OS Entry Point for Digger
 *
 * Replaces rp2350_main.c. Creates a FRANK OS window, initializes audio,
 * and runs the Digger game loop inside a windowed application.
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://rh1.tech
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "m-os-api.h"
#include "frankos-app.h"

#undef switch
#undef inline
#undef __force_inline
#undef abs

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "def.h"
#include "draw_api.h"
#include "sound.h"
#include "input.h"
#include "main.h"
#include "game.h"
#include "newsnd.h"
#include "frankos_app.h"

/* Digger log file (redirect to NULL on FRANK OS) */
FILE *digger_log = NULL;

/* Global app state */
digger_app_t *g_app = NULL;

/* Paint callback - defined in frankos_vid.c */
extern void digger_paint(hwnd_t hwnd);

/*
 * inir_defaults - Initialize default game settings (replaces INI file loading).
 */
static void inir_defaults(void) {
    dgstate.nplayers = 1;
    dgstate.diggers = 1;
    dgstate.curplayer = 0;
    dgstate.startlev = 1;
    dgstate.levfflag = false;
    dgstate.gauntlet = false;
    dgstate.gtime = 120;
    dgstate.timeout = false;
    dgstate.unlimlives = false;
    dgstate.ftime = 80000;  /* 80ms per frame = 12.5 Hz */
    dgstate.cgtime = 0;
    dgstate.randv = 0;

    soundflag = true;
    musicflag = true;
    volume = 1;

    setupsound = s1setupsound;
    killsound = s1killsound;
    soundoff = s1soundoff;
    setspkrt2 = s1setspkrt2;
    timer0 = s1timer0;
    timer2 = s1timer2;
    soundinitglob(512, DIGGER_AUDIO_SAMPLE_RATE);
}

/*
 * digger_event - Window event handler (runs on WM task).
 *
 * Handles keyboard events and window close. Communicates with the
 * game task via the shared digger_app_t state.
 */
bool digger_event(hwnd_t hwnd, const window_event_t *event) {
    if (!g_app)
        return false;

    if (event->type == WM_CLOSE) {
        g_app->closing = true;
        /* Wake up game task if blocked in getkey() */
        if (g_app->app_task)
            xTaskNotifyGive(g_app->app_task);
        return true;
    }

    if (event->type == WM_KEYDOWN) {
        uint8_t sc = event->key.scancode;
        g_app->key_state[sc] = 1;

        /* Push into key FIFO */
        if (g_app->key_fifo_len < DIGGER_KBLEN) {
            g_app->key_fifo[g_app->key_fifo_len] = sc;
            g_app->key_fifo_len++;
        }
        return true;
    }

    if (event->type == WM_KEYUP) {
        uint8_t sc = event->key.scancode;
        g_app->key_state[sc] = 0;
        return true;
    }

    return false;
}

/*
 * Main entry point for FRANK OS.
 */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Allocate app state */
    g_app = (digger_app_t *)pvPortMalloc(sizeof(digger_app_t));
    if (!g_app)
        return 1;
    memset(g_app, 0, sizeof(digger_app_t));

    g_app->app_task = xTaskGetCurrentTaskHandle();

    /* Allocate internal framebuffer: 320x240, 4-bit nibble-packed */
    g_app->framebuffer = (uint8_t *)pvPortMalloc(DIGGER_FB_STRIDE * DIGGER_FB_H);
    if (!g_app->framebuffer) {
        vPortFree(g_app);
        g_app = NULL;
        return 1;
    }
    memset(g_app->framebuffer, 0, DIGGER_FB_STRIDE * DIGGER_FB_H);

    /* Allocate audio buffer: stereo, 3528 frames */
    g_app->audio_buf = (int16_t *)pvPortMalloc(
        DIGGER_AUDIO_SAMPLES_PER_FRAME * 2 * sizeof(int16_t));
    if (g_app->audio_buf) {
        memset(g_app->audio_buf, 0,
               DIGGER_AUDIO_SAMPLES_PER_FRAME * 2 * sizeof(int16_t));
    }

    /* Create window: 320x200 client area + decorations */
    int16_t fw = DIGGER_WIDTH + 2 * THEME_BORDER_WIDTH;
    int16_t fh = DIGGER_HEIGHT + THEME_TITLE_HEIGHT + 2 * THEME_BORDER_WIDTH;
    int16_t x  = (DISPLAY_WIDTH - fw) / 2;
    int16_t y  = (DISPLAY_HEIGHT - TASKBAR_HEIGHT - fh) / 2;
    if (y < 0) y = 0;

    g_app->app_hwnd = wm_create_window(x, y, fw, fh, "Digger",
                                         WSTYLE_DIALOG,
                                         digger_event, digger_paint);
    if (g_app->app_hwnd == HWND_NULL) {
        vPortFree(g_app->framebuffer);
        if (g_app->audio_buf) vPortFree(g_app->audio_buf);
        vPortFree(g_app);
        g_app = NULL;
        return 1;
    }

    /* Store state in window user_data for paint callback */
    window_t *win = wm_get_window(g_app->app_hwnd);
    if (win) win->user_data = g_app;

    wm_show_window(g_app->app_hwnd);
    wm_set_focus(g_app->app_hwnd);
    taskbar_invalidate();

    /* Initialize PCM audio */
    if (g_app->audio_buf) {
        pcm_init(DIGGER_AUDIO_SAMPLE_RATE, 2);
        g_app->audio_initialized = true;
    }
    /* Initialize game with defaults and run */
    inir_defaults();
    maininit();
    mainprog();

    /* Cleanup */
    if (g_app->audio_initialized)
        pcm_cleanup();

    wm_destroy_window(g_app->app_hwnd);
    taskbar_invalidate();

    vPortFree(g_app->framebuffer);
    if (g_app->audio_buf) vPortFree(g_app->audio_buf);
    vPortFree(g_app);
    g_app = NULL;

    return 0;
}
