/*
 * FRANK OS — Video Player (standalone ELF app)
 *
 * Fullscreen 320x240x256 MPEG-1 video player using pl_mpeg.
 * Streams from SD card — no file size limit.
 * Launched by opening .mpg files from Navigator.
 * Space to pause, ESC to exit.
 *
 * MEMORY MODEL: same as Dendy — all mutable state heap-allocated in SRAM,
 * accessed through r9 register (-ffixed-r9).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "m-os-api.h"
#include "frankos-app.h"

#undef switch
#undef inline
#undef __force_inline
#undef abs

#define PLM_NO_STDIO
#define PLM_BUFFER_DEFAULT_SIZE (512 * 1024)
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

#include <string.h>

#define HID_KEY_ESCAPE  0x29
#define HID_KEY_SPACE   0x2C
#define AUDIO_BUF_SAMPLES  1152

typedef struct {
    volatile bool closing;
    volatile bool paused;
    uint8_t  key_state[256];
    void    *app_task;
    plm_t   *plm;
    FIL     *fil;
    int16_t *audio_buf;
    uint8_t *y_tab;
    uint8_t *dt;                /* dither tables: 12 * 1024 bytes, SRAM */
    uint8_t  saved_volume;
    int      video_w;
    int      video_h;
    int      offset_x;
    int      offset_y;
} app_globals_t;

register app_globals_t *G asm("r9");

/* ======================================================================
 * pl_mpeg buffer callbacks — stream from FatFS
 * ====================================================================== */

static void plm_load_cb(plm_buffer_t *buf, void *user) {
    FIL *fil = (FIL *)user;
    if (buf->discard_read_bytes)
        plm_buffer_discard_read_bytes(buf);
    size_t bytes_available = buf->capacity - buf->length;
    UINT br = 0;
    f_read(fil, buf->bytes + buf->length, (UINT)bytes_available, &br);
    buf->length += br;
    if (br == 0)
        buf->has_ended = TRUE;
}

static void plm_seek_cb(plm_buffer_t *buf, size_t offset, void *user) {
    (void)buf;
    f_lseek((FIL *)user, (FSIZE_t)offset);
}

static size_t plm_tell_cb(plm_buffer_t *buf, void *user) {
    (void)buf;
    return (size_t)f_tell((FIL *)user);
}

/* ======================================================================
 * RGB332 palette
 * ====================================================================== */

static void setup_palette(void) {
    for (int i = 0; i < 256; i++) {
        int r3 = (i >> 5) & 7;
        int g3 = (i >> 2) & 7;
        int b2 = i & 3;
        uint32_t rgb = ((uint32_t)(r3 * 255 / 7) << 16)
                     | ((uint32_t)(g3 * 255 / 7) << 8)
                     | (uint32_t)(b2 * 255 / 3);
        display_set_palette_entry((uint8_t)i, rgb);
    }
}

/* ======================================================================
 * YCbCr → RGB332, BT.601 limited-range, 2×2 blocks, SRAM line buffers
 * ====================================================================== */

/* Dither tables: 4 Bayer positions × 3 channels (R,G,B) = 12 tables.
 * Each table is 1024 bytes, indexed by (scaled_Y + chroma_delta + 256).
 * Clamping + quantization + dither threshold baked in.
 * Total: 12 KB in SRAM.  Inner loop: 3 loads + 2 ORs per pixel. */

#define DT_SZ   1024
#define DT_BIAS 256

static void init_tables(void) {
    int p, i;

    G->y_tab = (uint8_t *)pvPortMalloc(256);
    for (i = 0; i < 256; i++) {
        int v = ((i - 16) * 76309) >> 16;
        if (v < 0) v = 0; else if (v > 255) v = 255;
        G->y_tab[i] = (uint8_t)v;
    }

    /* 2×2 Bayer: {{0,2},{3,1}}/4  →  thresholds scaled to quantization step
     * R/G (3 bits, step≈36): {0,18,27,9}   B (2 bits, step=85): {0,42,64,21} */
    int rg[4] = {0, 18, 27, 9};
    int  b[4] = {0, 42, 64, 21};

    G->dt = (uint8_t *)pvPortMalloc(12 * DT_SZ);
    for (p = 0; p < 4; p++) {
        uint8_t *dr = G->dt + (p * 3 + 0) * DT_SZ;
        uint8_t *dg = G->dt + (p * 3 + 1) * DT_SZ;
        uint8_t *db = G->dt + (p * 3 + 2) * DT_SZ;
        for (i = 0; i < DT_SZ; i++) {
            int v = i - DT_BIAS;
            int rv = v + rg[p]; if (rv < 0) rv = 0; if (rv > 255) rv = 255;
            int gv = v + rg[p]; if (gv < 0) gv = 0; if (gv > 255) gv = 255;
            int bv = v +  b[p]; if (bv < 0) bv = 0; if (bv > 255) bv = 255;
            dr[i] = (uint8_t)(rv & 0xE0);
            dg[i] = (uint8_t)((gv >> 3) & 0x1C);
            db[i] = (uint8_t)(bv >> 6);
        }
    }
}

#define LINE_BUF_W 336

static void on_video(plm_t *mpeg, plm_frame_t *frame, void *user) {
    (void)mpeg; (void)user;

    uint8_t *fb = display_get_framebuffer();
    if (!fb) return;

    int cols = (int)frame->width >> 1;
    int rows = (int)frame->height >> 1;
    int yw = (int)frame->y.width;
    int cw = (int)frame->cb.width;
    int ox = G->offset_x;
    int oy = G->offset_y;
    const uint8_t *ytab = G->y_tab;

    if (cols > 160) cols = 160;
    if (rows > 120) rows = 120;

    /* Pre-biased dither table pointers — TL/TR/BL/BR × R/G/B */
    const uint8_t *r0 = G->dt + 0*DT_SZ + DT_BIAS;
    const uint8_t *g0 = G->dt + 1*DT_SZ + DT_BIAS;
    const uint8_t *b0 = G->dt + 2*DT_SZ + DT_BIAS;
    const uint8_t *r1 = G->dt + 3*DT_SZ + DT_BIAS;
    const uint8_t *g1 = G->dt + 4*DT_SZ + DT_BIAS;
    const uint8_t *b1 = G->dt + 5*DT_SZ + DT_BIAS;
    const uint8_t *r2 = G->dt + 6*DT_SZ + DT_BIAS;
    const uint8_t *g2 = G->dt + 7*DT_SZ + DT_BIAS;
    const uint8_t *b2 = G->dt + 8*DT_SZ + DT_BIAS;
    const uint8_t *r3 = G->dt + 9*DT_SZ + DT_BIAS;
    const uint8_t *g3 = G->dt +10*DT_SZ + DT_BIAS;
    const uint8_t *b3 = G->dt +11*DT_SZ + DT_BIAS;

    uint8_t yl0[LINE_BUF_W], yl1[LINE_BUF_W];
    uint8_t cbl[LINE_BUF_W/2], crl[LINE_BUF_W/2];

    for (int row = 0; row < rows; row++) {
        memcpy(yl0, frame->y.data  + row*2*yw,      cols*2);
        memcpy(yl1, frame->y.data  + row*2*yw + yw,  cols*2);
        memcpy(cbl, frame->cb.data + row*cw,          cols);
        memcpy(crl, frame->cr.data + row*cw,          cols);

        int di = (oy + row*2) * 320 + ox;
        int yi = 0;

        for (int col = 0; col < cols; col++) {
            int cr = crl[col] - 128;
            int cb = cbl[col] - 128;
            int rd = (cr * 104597) >> 16;
            int gd = (cb * 25674 + cr * 53278) >> 16;
            int bd = (cb * 132201) >> 16;

            int yy;
            yy = ytab[yl0[yi]];
            fb[di]     = r0[yy+rd] | g0[yy-gd] | b0[yy+bd];
            yy = ytab[yl0[yi+1]];
            fb[di+1]   = r1[yy+rd] | g1[yy-gd] | b1[yy+bd];
            yy = ytab[yl1[yi]];
            fb[di+320] = r2[yy+rd] | g2[yy-gd] | b2[yy+bd];
            yy = ytab[yl1[yi+1]];
            fb[di+321] = r3[yy+rd] | g3[yy-gd] | b3[yy+bd];

            yi += 2;
            di += 2;
        }
    }
}

/* ======================================================================
 * Audio callback
 * ====================================================================== */

static void on_audio(plm_t *mpeg, plm_samples_t *samples, void *user) {
    (void)mpeg; (void)user;
    int16_t *out = G->audio_buf;
    int count = (int)samples->count;
    for (int i = 0; i < count * 2; i++) {
        int v = (int)(samples->interleaved[i] * 32767.0f);
        if (v >  32767) v =  32767;
        if (v < -32767) v = -32767;
        out[i] = (int16_t)v;
    }
    pcm_write(out, count);
}

/* ======================================================================
 * Input
 * ====================================================================== */

static void process_input(void) {
    keyboard_poll();
    app_key_event_t ev;
    while (keyboard_get_event(&ev)) {
        if (ev.hid_code < 256)
            G->key_state[ev.hid_code] = ev.pressed ? 1 : 0;
        if (!ev.pressed) continue;
        if (ev.hid_code == HID_KEY_ESCAPE) { G->closing = true; return; }
        if (ev.hid_code == HID_KEY_SPACE)    G->paused = !G->paused;
    }
}

/* ======================================================================
 * Entry point
 * ====================================================================== */

int main(int argc, char **argv) {
    if (argc < 2 || !argv[1] || !argv[1][0]) {
        dialog_show(HWND_NULL, "Video Player",
                    "Open a .mpg video file from\n"
                    "the Navigator to play.\n\n"
                    "Space = Pause, Esc = Exit",
                    DLG_ICON_INFO, DLG_BTN_OK);
        return 0;
    }

    {
        extern uint32_t __app_flags(void);
        uintptr_t code_addr = (uintptr_t)__app_flags;
        if (code_addr >= 0x15000000 && code_addr < 0x15800000) {
            dialog_show(HWND_NULL, "Video Player",
                        "Not enough SRAM to run.\n"
                        "Close other apps and retry.",
                        DLG_ICON_ERROR, DLG_BTN_OK);
            return 0;
        }
    }

    app_globals_t *globals = (app_globals_t *)pvPortMalloc(sizeof(app_globals_t));
    if (!globals) {
        dialog_show(HWND_NULL, "Video Player", "Not enough memory.",
                    DLG_ICON_ERROR, DLG_BTN_OK);
        return 1;
    }
    memset(globals, 0, sizeof(app_globals_t));
    G = globals;
    G->app_task = xTaskGetCurrentTaskHandle();
    init_tables();

    /* Open file */
    G->fil = (FIL *)pvPortMalloc(sizeof(FIL));
    if (!G->fil || f_open(G->fil, argv[1], FA_READ) != FR_OK) {
        dialog_show(HWND_NULL, "Video Player", "Cannot open file.",
                    DLG_ICON_ERROR, DLG_BTN_OK);
        if (G->fil) vPortFree(G->fil);
        vPortFree(globals);
        return 1;
    }

    /* Create pl_mpeg with FatFS streaming */
    FSIZE_t file_size = f_size(G->fil);
    plm_buffer_t *buf = plm_buffer_create_with_callbacks(
        plm_load_cb, plm_seek_cb, plm_tell_cb,
        (size_t)file_size, G->fil);
    G->plm = plm_create_with_buffer(buf, TRUE);
    if (!G->plm) {
        dialog_show(HWND_NULL, "Video Player", "Not a valid MPEG-1 file.",
                    DLG_ICON_ERROR, DLG_BTN_OK);
        f_close(G->fil); vPortFree(G->fil); vPortFree(globals);
        return 1;
    }

    G->video_w = plm_get_width(G->plm);
    G->video_h = plm_get_height(G->plm);
    if (G->video_w <= 0 || G->video_h <= 0) {
        dialog_show(HWND_NULL, "Video Player", "Invalid video dimensions.",
                    DLG_ICON_ERROR, DLG_BTN_OK);
        plm_destroy(G->plm); vPortFree(globals);
        return 1;
    }

    G->offset_x = (320 - G->video_w) / 2;
    G->offset_y = (240 - G->video_h) / 2;
    if (G->offset_x < 0) G->offset_x = 0;
    if (G->offset_y < 0) G->offset_y = 0;

    serial_printf("video: %dx%d @ %d fps, audio %d Hz\n",
                  G->video_w, G->video_h,
                  (int)plm_get_framerate(G->plm),
                  plm_get_samplerate(G->plm));

    G->audio_buf = (int16_t *)pvPortMalloc(AUDIO_BUF_SAMPLES * 2 * sizeof(int16_t));
    if (!G->audio_buf) {
        dialog_show(HWND_NULL, "Video Player", "Not enough memory for audio.",
                    DLG_ICON_ERROR, DLG_BTN_OK);
        plm_destroy(G->plm); vPortFree(globals);
        return 1;
    }

    if (display_set_video_mode(VIDEO_MODE_320x240x256) != 0) {
        dialog_show(HWND_NULL, "Video Player", "Failed to switch video mode.",
                    DLG_ICON_ERROR, DLG_BTN_OK);
        vPortFree(G->audio_buf); plm_destroy(G->plm); vPortFree(globals);
        return 1;
    }

    setup_palette();
    display_clear(0);

    /* Audio */
    int samplerate = plm_get_samplerate(G->plm);
    if (samplerate > 0) {
        pcm_init(samplerate, 2);
        plm_set_audio_enabled(G->plm, TRUE);
    } else {
        plm_set_audio_enabled(G->plm, FALSE);
    }

    {
        typedef uint8_t (*get_vol_t)(void);
        typedef void (*set_vol_t)(uint8_t);
        G->saved_volume = ((get_vol_t)_sys_table_ptrs[535])();
        uint8_t new_vol = (G->saved_volume >= 2) ? G->saved_volume - 2 : 0;
        ((set_vol_t)_sys_table_ptrs[534])(new_vol);
    }

    /* Callbacks */
    plm_set_video_decode_callback(G->plm, on_video, NULL);
    if (samplerate > 0) {
        plm_set_audio_decode_callback(G->plm, on_audio, NULL);
        plm_set_audio_lead_time(G->plm, (double)AUDIO_BUF_SAMPLES / samplerate);
    }

    /* Main loop */
    uint32_t last_tick = xTaskGetTickCount();
    while (!G->closing) {
        process_input();
        if (G->closing) break;

        if (G->paused) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
            last_tick = xTaskGetTickCount();
            continue;
        }

        uint32_t now = xTaskGetTickCount();
        uint32_t elapsed_ms = now - last_tick;
        last_tick = now;
        if (elapsed_ms > 100) elapsed_ms = 100;

        plm_decode(G->plm, (double)elapsed_ms * 0.001);

        if (plm_has_ended(G->plm)) break;
    }

    /* Cleanup */
    {
        typedef void (*set_vol_t)(uint8_t);
        ((set_vol_t)_sys_table_ptrs[534])(G->saved_volume);
    }
    if (samplerate > 0) pcm_cleanup();

    plm_destroy(G->plm);
    f_close(G->fil);
    vPortFree(G->fil);

    display_set_video_mode(VIDEO_MODE_640x480x16);
    {
        typedef void (*fn_t)(void);
        ((fn_t)_sys_table_ptrs[533])();
    }
    taskbar_invalidate();

    vPortFree(G->audio_buf);
    vPortFree(G->y_tab);
    vPortFree(G->dt);
    vPortFree(globals);
    return 0;
}

uint32_t __app_flags(void) { return APPFLAG_SINGLETON; }
