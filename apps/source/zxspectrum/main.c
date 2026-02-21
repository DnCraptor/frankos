/*
 * FRANK OS — ZX Spectrum 48K Emulator (standalone ELF app)
 *
 * Uses zx2040 by antirez (https://github.com/antirez/zx2040) as the
 * emulation core: header-only Z80 CPU + ZX Spectrum ULA emulation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "m-os-api.h"
#include "frankos-app.h"

/*
 * m-os-api.h defines:
 *   #define switch DO_NOT_USE_SWITCH
 *   #define inline __force_inline
 * These break the zx2040 headers which use switch statements and inline
 * functions extensively.  Undo them before pulling in the emulator core.
 */
#undef switch
#undef inline
#undef __force_inline

/* Disable audio — SPEAKER_PIN == -1 skips audio sampling in zx_exec */
#define SPEAKER_PIN -1

/* Override assert for freestanding environment */
#define CHIPS_ASSERT(c) ((void)(c))

/* mem.h calls vram_set_dirty_*() for SPI display tracking (zx2040).
 * We don't use partial-scanline tracking, so provide no-op stubs. */
static inline void vram_set_dirty_bitmap(uint16_t addr) { (void)addr; }
static inline void vram_set_dirty_attr(uint16_t addr)   { (void)addr; }

/* Pull in the emulator implementation.
 * Include order matches zx.c: chips_common → mem → z80 → kbd → clk → zx
 *
 * Compile entire emulation core at -O2 for speed (the hot path is z80_tick
 * called from _zx_tick called from zx_exec — all must be fast). */
#define CHIPS_IMPL
#pragma GCC optimize("O2")
#include "chips_common.h"
#include "mem.h"
#include "z80.h"
#include "kbd.h"
#include "clk.h"
#include "zx.h"
#pragma GCC optimize("Oz")
#include "zx-roms.h"

/* Menu command IDs */
#define CMD_LOAD_TAP  1
#define CMD_RESET     2
#define CMD_EXIT      3

/* TAP loading state */
static FIL    tap_file;
static bool   tap_loaded;

static void handle_tape_trap(void *ud);
static void load_tap_file(const char *path);

/* ======================================================================
 * Emulator state
 * ====================================================================== */

static zx_t         *zx;          /* heap-allocated in SRAM */
static hwnd_t        app_hwnd;
static volatile bool closing;
static void         *app_task;

/* Border: 32 px left/right, 24 px top/bottom → 320×240 client area */
#define BORDER_H  32
#define BORDER_V  24
#define CLIENT_W  (256 + 2 * BORDER_H)   /* 320 */
#define CLIENT_H  (192 + 2 * BORDER_V)   /* 240 */

/* ======================================================================
 * Colour mapping:  ZX 16-colour → CGA 16-colour
 * ====================================================================== */

static const uint8_t zx_to_cga[16] = {
    COLOR_BLACK,          /*  0  Black         */
    COLOR_BLUE,           /*  1  Blue          */
    COLOR_RED,            /*  2  Red           */
    COLOR_MAGENTA,        /*  3  Magenta       */
    COLOR_GREEN,          /*  4  Green         */
    COLOR_CYAN,           /*  5  Cyan          */
    COLOR_BROWN,          /*  6  Yellow (dim)  */
    COLOR_LIGHT_GRAY,     /*  7  White  (dim)  */
    COLOR_BLACK,          /*  8  Bright Black  */
    COLOR_LIGHT_BLUE,     /*  9  Bright Blue   */
    COLOR_LIGHT_RED,      /* 10  Bright Red    */
    COLOR_LIGHT_MAGENTA,  /* 11  Bright Magenta*/
    COLOR_LIGHT_GREEN,    /* 12  Bright Green  */
    COLOR_LIGHT_CYAN,     /* 13  Bright Cyan   */
    COLOR_YELLOW,         /* 14  Bright Yellow */
    COLOR_WHITE,          /* 15  Bright White  */
};

/* ======================================================================
 * Paint handler — 1× native resolution (256×192) + border
 * ====================================================================== */

static void zx_paint(hwnd_t hwnd) {
    wd_begin(hwnd);

    uint8_t border = zx_to_cga[zx->border_color & 7];

    /* Draw border — four rectangles around the 256×192 bitmap area */
    wd_fill_rect(0, 0, CLIENT_W, BORDER_V, border);                       /* top */
    wd_fill_rect(0, BORDER_V + 192, CLIENT_W, BORDER_V, border);          /* bottom */
    wd_fill_rect(0, BORDER_V, BORDER_H, 192, border);                     /* left */
    wd_fill_rect(BORDER_H + 256, BORDER_V, BORDER_H, 192, border);        /* right */

    /* Decode ZX VRAM at native 1× resolution */
    uint8_t *vmem = zx->ram[0];

    for (int y = 0; y < 192; y++) {
        /* ZX Spectrum VRAM address interleaving */
        int addr = ((y & 0xC0) << 5) | ((y & 0x07) << 8) | ((y & 0x38) << 2);
        int py = BORDER_V + y;

        /* Walk 32 columns × 8 pixels, batching runs of same colour */
        uint8_t run_color = 0xFF;   /* sentinel */
        int     run_start = BORDER_H;

        for (int col = 0; col < 32; col++) {
            uint8_t byte = vmem[addr + col];
            uint8_t attr = vmem[0x1800 + ((y >> 3) << 5) + col];

            uint8_t ink    = attr & 0x07;
            uint8_t paper  = (attr >> 3) & 0x07;
            uint8_t bright = (attr & 0x40) ? 8 : 0;
            if ((attr & 0x80) && (zx->blink_counter & 0x10)) {
                uint8_t tmp = ink; ink = paper; paper = tmp;
            }
            uint8_t fg = zx_to_cga[ink + bright];
            uint8_t bg = zx_to_cga[paper + bright];

            for (int bit = 7; bit >= 0; bit--) {
                uint8_t color = (byte & (1 << bit)) ? fg : bg;
                int px = BORDER_H + col * 8 + (7 - bit);

                if (color != run_color) {
                    if (run_color != 0xFF) {
                        wd_hline(run_start, py, px - run_start, run_color);
                    }
                    run_color = color;
                    run_start = px;
                }
            }
        }
        /* Flush final run */
        if (run_color != 0xFF) {
            wd_hline(run_start, py, BORDER_H + 256 - run_start, run_color);
        }
    }
    wd_end();
}

/* ======================================================================
 * Keyboard mapping:  HID scancode → ZX key code
 * ====================================================================== */

static int hid_to_zx(uint8_t scancode) {
    if (scancode >= 0x04 && scancode <= 0x1D)     /* a-z */
        return 'a' + (scancode - 0x04);
    if (scancode >= 0x1E && scancode <= 0x26)      /* 1-9 */
        return '1' + (scancode - 0x1E);
    if (scancode == 0x27) return '0';
    if (scancode == 0x28) return 0x0D;             /* Enter */
    if (scancode == 0x2C) return ' ';              /* Space */
    if (scancode == 0x2A) return 0x0C;             /* Backspace → Delete */
    if (scancode == 0x50) return 0x08;             /* Left */
    if (scancode == 0x4F) return 0x09;             /* Right */
    if (scancode == 0x51) return 0x0A;             /* Down */
    if (scancode == 0x52) return 0x0B;             /* Up */
    return -1;
}

static uint8_t prev_modifiers;

static void handle_modifiers(uint8_t mods, bool is_down) {
    (void)is_down;
    uint8_t changed = mods ^ prev_modifiers;
    prev_modifiers = mods;

    if (changed & KMOD_SHIFT) {
        if (mods & KMOD_SHIFT) zx_key_down(zx, 0x00);
        else                   zx_key_up(zx, 0x00);
    }
    if (changed & KMOD_CTRL) {
        if (mods & KMOD_CTRL)  zx_key_down(zx, 0x0F);
        else                   zx_key_up(zx, 0x0F);
    }
}

/* Forward declarations — noinline prevents compiler pulling them into .text */
static bool handle_menu_command(hwnd_t hwnd, int command_id)
    __attribute__((noinline));
static void setup_menu(hwnd_t hwnd)
    __attribute__((noinline));

/* ======================================================================
 * Event handler
 * ====================================================================== */

static bool zx_event(hwnd_t hwnd, const window_event_t *ev) {
    if (ev->type == WM_COMMAND) {
        if (ev->command.id == DLG_RESULT_FILE) {
            load_tap_file(file_dialog_get_path());
            return true;
        }
        if (ev->command.id == DLG_RESULT_CANCEL)
            return true;  /* user cancelled — ignore */
        return handle_menu_command(hwnd, ev->command.id);
    }

    if (ev->type == WM_KEYDOWN || ev->type == WM_KEYUP) {
        int zx_key = hid_to_zx(ev->key.scancode);
        if (zx_key >= 0) {
            if (ev->type == WM_KEYDOWN) zx_key_down(zx, zx_key);
            else                         zx_key_up(zx, zx_key);
        }
        handle_modifiers(ev->key.modifiers, ev->type == WM_KEYDOWN);
        return true;
    }

    if (ev->type == WM_CLOSE) {
        closing = true;
        xTaskNotifyGive(app_task);
        return true;
    }

    return false;
}

/* ======================================================================
 * Entry point
 * ====================================================================== */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    app_task = xTaskGetCurrentTaskHandle();

    /* Allocate zx_t in SRAM */
    zx = (zx_t *)pvPortMalloc(sizeof(zx_t));
    if (!zx) {
        goutf("ZX Spectrum: not enough SRAM for emulator (%u bytes)\n",
              (unsigned)sizeof(zx_t));
        return 1;
    }
    memset(zx, 0, sizeof(zx_t));

    /* Initialise ZX Spectrum 48K emulator */
    zx_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.type = ZX_TYPE_48K;
    desc.joystick_type = ZX_JOYSTICKTYPE_NONE;
    desc.roms.zx48k.ptr = dump_amstrad_zx48k_bin;
    desc.roms.zx48k.size = sizeof(dump_amstrad_zx48k_bin);
    zx_init(zx, &desc);

    /* Window: 320×240 client area (256×192 + border) + menu bar */
    int16_t fw = CLIENT_W + 2 * THEME_BORDER_WIDTH;
    int16_t fh = CLIENT_H + THEME_TITLE_HEIGHT + THEME_MENU_HEIGHT
               + 2 * THEME_BORDER_WIDTH;
    int16_t x  = (DISPLAY_WIDTH  - fw) / 2;
    int16_t y  = (DISPLAY_HEIGHT - TASKBAR_HEIGHT - fh) / 2;
    if (y < 0) y = 0;

    app_hwnd = wm_create_window(x, y, fw, fh, "ZX Spectrum",
                                WSTYLE_DIALOG | WF_MENUBAR,
                                zx_event, zx_paint);
    if (app_hwnd == HWND_NULL) {
        vPortFree(zx);
        return 1;
    }

    setup_menu(app_hwnd);
    wm_show_window(app_hwnd);
    wm_set_focus(app_hwnd);
    taskbar_invalidate();

    serial_printf("ZX: SP=%p zx=%p sizeof(zx_t)=%u\n",
                  __builtin_frame_address(0), (void *)zx,
                  (unsigned)sizeof(zx_t));

    /* Main emulation loop.
     * zx_exec() takes MICROSECONDS — 20000 µs = 20 ms = 1 ZX frame. */
    uint32_t frame = 0;
    uint32_t t0 = xTaskGetTickCount();

    while (!closing) {
        /* Run z80 in 8 × 2500 µs bursts = one 50-Hz frame (20 ms).
         * vTaskSuspendAll prevents FreeRTOS task switches during each
         * burst (no CPU stolen by input/compositor tasks) while keeping
         * all hardware interrupts enabled (PS/2 PIO ISR captures mouse
         * bytes into its ring buffer uninterrupted).  Between bursts,
         * xTaskResumeAll lets pending context switches run — input_task
         * polls keyboard, compositor processes mouse and moves cursor. */
        for (int burst = 0; burst < 8 && !closing; burst++) {
            vTaskSuspendAll();
            zx_exec(zx, 2500);
            xTaskResumeAll();
        }

        ++frame;
        wm_invalidate(app_hwnd);
        vTaskDelay(pdMS_TO_TICKS(1));

        if (frame == 50) {
            uint32_t elapsed = xTaskGetTickCount() - t0;
            serial_printf("ZX: 50fr %ums (%u fps)\n",
                          (unsigned)elapsed,
                          (unsigned)(50000 / elapsed));
        }
    }

    wm_destroy_window(app_hwnd);
    taskbar_invalidate();
    if (tap_loaded) {
        f_close(&tap_file);
        tap_loaded = false;
    }
    zx->tape_trap = NULL;
    vPortFree(zx);
    return 0;
}

/* ======================================================================
 * Menu setup (in .text.startup — called once from main)
 * ====================================================================== */

static void setup_menu(hwnd_t hwnd) {
    menu_bar_t bar;
    memset(&bar, 0, sizeof(bar));
    bar.menu_count = 1;

    menu_def_t *file = &bar.menus[0];
    strncpy(file->title, "File", sizeof(file->title) - 1);
    file->item_count = 4;

    strncpy(file->items[0].text, "Load TAP...", 19);
    file->items[0].command_id = CMD_LOAD_TAP;

    strncpy(file->items[1].text, "Reset", 19);
    file->items[1].command_id = CMD_RESET;

    file->items[2].flags = MIF_SEPARATOR;

    strncpy(file->items[3].text, "Exit", 19);
    file->items[3].command_id = CMD_EXIT;

    menu_set(hwnd, &bar);
}

/* ======================================================================
 * Menu command handler (in .text.startup — called via noinline from zx_event)
 * ====================================================================== */

static bool handle_menu_command(hwnd_t hwnd, int command_id) {
    if (command_id == CMD_EXIT) {
        window_event_t ce;
        memset(&ce, 0, sizeof(ce));
        ce.type = WM_CLOSE;
        wm_post_event(hwnd, &ce);
        return true;
    }
    if (command_id == CMD_RESET) {
        zx_desc_t desc;
        memset(&desc, 0, sizeof(desc));
        desc.type = ZX_TYPE_48K;
        desc.joystick_type = ZX_JOYSTICKTYPE_NONE;
        desc.roms.zx48k.ptr = dump_amstrad_zx48k_bin;
        desc.roms.zx48k.size = sizeof(dump_amstrad_zx48k_bin);
        zx_init(zx, &desc);
        wm_invalidate(hwnd);
        return true;
    }
    if (command_id == CMD_LOAD_TAP) {
        file_dialog_open(hwnd, "Load TAP", "/", ".tap");
        return true;
    }
    return false;
}

/* ======================================================================
 * TAP file loading
 * ====================================================================== */

static void load_tap_file(const char *path) {
    if (tap_loaded) {
        f_close(&tap_file);
        tap_loaded = false;
        zx->tape_trap = NULL;
    }
    FRESULT fr = f_open(&tap_file, path, FA_READ);
    if (fr != FR_OK) {
        serial_printf("ZX: TAP open failed: %s (err %d)\n", path, fr);
        return;
    }
    tap_loaded = true;
    zx->tape_trap = handle_tape_trap;
    zx->tape_trap_ud = zx;
    serial_printf("ZX: TAP loaded: %s\n", path);
}

static void handle_tape_trap(void *ud) {
    zx_t *sys = (zx_t *)ud;

    /* Read Z80 registers set by ROM before calling LD-BYTES:
     *   A  = expected flag byte (0x00 header, 0xFF data)
     *   IX = destination address
     *   DE = expected data length (payload only)
     *   CF = 1: LOAD, 0: VERIFY */
    uint8_t  expected_flag = sys->cpu.a;
    uint16_t dest          = sys->cpu.ix;
    uint16_t expected_len  = sys->cpu.de;
    bool     is_load       = (sys->cpu.f & Z80_CF) != 0;

    /* VERIFY mode: just pretend success */
    if (!is_load) {
        sys->cpu.f |= Z80_CF;
        sys->pins = z80_prefetch(&sys->cpu, 0x05E2);
        return;
    }

    /* Read 2-byte block length (little-endian) from TAP file */
    uint8_t hdr[3];
    UINT br;
    FRESULT fr = f_read(&tap_file, hdr, 2, &br);
    if (fr != FR_OK || br < 2) {
        /* EOF or read error — rewind for next LOAD attempt */
        serial_printf("ZX: TAP EOF/error, rewinding\n");
        f_lseek(&tap_file, 0);
        sys->cpu.f &= ~Z80_CF;
        sys->pins = z80_prefetch(&sys->cpu, 0x05E2);
        return;
    }

    uint16_t block_len = hdr[0] | ((uint16_t)hdr[1] << 8);
    if (block_len < 2) {
        /* Malformed block */
        sys->cpu.f &= ~Z80_CF;
        sys->pins = z80_prefetch(&sys->cpu, 0x05E2);
        return;
    }

    /* Read 1-byte flag */
    fr = f_read(&tap_file, &hdr[2], 1, &br);
    if (fr != FR_OK || br < 1) {
        sys->cpu.f &= ~Z80_CF;
        sys->pins = z80_prefetch(&sys->cpu, 0x05E2);
        return;
    }
    uint8_t flag = hdr[2];

    /* Flag mismatch: skip this block, let ROM retry with next block */
    if (flag != expected_flag) {
        uint16_t remaining = block_len - 1; /* skip flag+payload+checksum */
        f_lseek(&tap_file, f_tell(&tap_file) + remaining);
        sys->cpu.f &= ~Z80_CF;
        sys->pins = z80_prefetch(&sys->cpu, 0x05E2);
        return;
    }

    /* data_len = block_len - 2 (subtract flag byte and checksum byte) */
    uint16_t data_len = block_len - 2;
    uint16_t to_load = (expected_len < data_len) ? expected_len : data_len;

    serial_printf("ZX: TAP block flag=%02X len=%u dest=%04X load=%u\n",
                  flag, data_len, dest, to_load);

    /* Read and copy payload into Z80 RAM in 128-byte chunks */
    uint8_t xor_check = flag;
    uint8_t buf[128];
    uint16_t loaded = 0;

    while (loaded < to_load) {
        uint16_t chunk = to_load - loaded;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);
        fr = f_read(&tap_file, buf, chunk, &br);
        if (fr != FR_OK || br == 0) break;
        for (UINT i = 0; i < br; i++) {
            mem_wr(&sys->mem, dest++, buf[i]);
            xor_check ^= buf[i];
            loaded++;
        }
    }

    /* If data_len > expected_len, read remaining bytes for checksum */
    if (data_len > expected_len) {
        uint16_t skip = data_len - expected_len;
        while (skip > 0) {
            uint16_t chunk = (skip > sizeof(buf)) ? sizeof(buf) : skip;
            fr = f_read(&tap_file, buf, chunk, &br);
            if (fr != FR_OK || br == 0) break;
            for (UINT i = 0; i < br; i++)
                xor_check ^= buf[i];
            skip -= br;
        }
    }

    /* Read and verify checksum byte */
    uint8_t file_checksum;
    fr = f_read(&tap_file, &file_checksum, 1, &br);
    if (fr == FR_OK && br == 1)
        xor_check ^= file_checksum;

    /* Update Z80 registers */
    sys->cpu.ix += to_load;
    sys->cpu.de -= to_load;

    if (xor_check == 0) {
        /* Success */
        sys->cpu.f |= Z80_CF;
        sys->cpu.af2 |= 0x40;  /* bit 6 of F' — ROM internal flag */
    } else {
        serial_printf("ZX: TAP checksum error (xor=%02X)\n", xor_check);
        sys->cpu.f &= ~Z80_CF;
    }

    sys->pins = z80_prefetch(&sys->cpu, 0x05E2);

    /* If at EOF, rewind for multi-load games */
    if (f_eof(&tap_file))
        f_lseek(&tap_file, 0);
}

