#pragma GCC optimize("Ofast")

#include <pico.h>
#include <pico/sync.h>
#include <pico/multicore.h>
#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/irq.h>

#define PIO_VGA (pio0)
#define beginVGA_PIN (6)
#define VGA_DMA_IRQ (DMA_IRQ_0)
#define palette16_mask 0xc0c0

#define TMPL_LINE           0xC0
#define TMPL_HS             0x80
#define TMPL_VS             0x40
#define TMPL_VHS            0x00

static int _SM_VGA = -1;
static int dma_chan_ctrl;
static int dma_chan;

static semaphore_t vga_start_semaphore;
volatile static void (*_fnc)();

uint16_t pio_program_VGA_instructions[] = {
    //     .wrap_target
    0x6008, //  0: out    pins, 8
    //     .wrap
};

const struct pio_program pio_program_VGA = {
    .instructions = pio_program_VGA_instructions,
    .length = 1,
    .origin = -1,
};

const static uint8_t conv0[] = { 0b00, 0b00, 0b01, 0b10, 0b10, 0b10, 0b11, 0b11 };
const static uint8_t conv1[] = { 0b00, 0b01, 0b01, 0b01, 0b10, 0b11, 0b11, 0b11 };

static uint32_t bg_color[2];
static uint16_t __scratch_y("vga_driver") palette[16*4];

inline static uint8_t* __time_critical_func(dma_handler_VGA_impl)() {
}

static void __time_critical_func(dma_handler_VGA)() {
    dma_hw->ints0 = 1u << dma_chan_ctrl;
    dma_channel_set_read_addr(dma_chan_ctrl, dma_handler_VGA_impl(), false);
}

static void vga_set_bgcolor(const uint32_t color888) {
    const uint8_t b = (color888 & 0xff) / 42;
    const uint8_t r = (color888 >> 16 & 0xff) / 42;
    const uint8_t g = (color888 >> 8 & 0xff) / 42;
    const uint8_t c_hi = conv0[r] << 4 | conv0[g] << 2 | conv0[b];
    const uint8_t c_lo = conv1[r] << 4 | conv1[g] << 2 | conv1[b];
    bg_color[0] = ((c_hi << 8 | c_lo) & 0x3f3f | palette16_mask) << 16 |
                  ((c_hi << 8 | c_lo) & 0x3f3f | palette16_mask);
    bg_color[1] = ((c_lo << 8 | c_hi) & 0x3f3f | palette16_mask) << 16 |
                  ((c_lo << 8 | c_hi) & 0x3f3f | palette16_mask);
}

static inline uint8_t vga_color_to_output(uint8_t r8, uint8_t g8, uint8_t b8) {
    uint8_t r2 = r8 >> 6;
    uint8_t g2 = g8 >> 6;
    uint8_t b2 = b8 >> 6;
    return TMPL_LINE | (r2 << 4) | (g2 << 2) | b2;
}

static void init_palette() {
    extern const uint32_t default_palette_rgb888[16];
    for (int i = 0; i < 16; ++i) {
        uint32_t c = default_palette_rgb888[i];
        palette[i] = vga_color_to_output(c >> 16, c >> 8, c);
    }
    for (int i = 16; i < 64; ++i) {
        palette[i] = TMPL_LINE;
    }
    bg_color[0] = (bg_color[0] & 0x3f3f3f3f) | palette16_mask | (palette16_mask << 16);
    bg_color[1] = (bg_color[1] & 0x3f3f3f3f) | palette16_mask | (palette16_mask << 16);
    for (int i = 0; i < 64; i++) {
        palette[i] = (palette[i] & 0x3f3f) | palette16_mask;
    }
}

void vga_init(void) {
    static uint32_t* initial_lines_pattern[4]; // TODO: remove W/A
    if (_SM_VGA != -1) return; // do not init it twice
    //инициализация PIO
    //загрузка программы в один из PIO
    uint offset = pio_add_program(PIO_VGA, &pio_program_VGA);
    _SM_VGA = pio_claim_unused_sm(PIO_VGA, true);
    uint sm = _SM_VGA;
    for (int i = 0; i < 8; i++) {
        gpio_init(beginVGA_PIN + i);
        gpio_set_dir(beginVGA_PIN + i, GPIO_OUT);
        pio_gpio_init(PIO_VGA, beginVGA_PIN + i);
    }; //резервируем под выход PIO
    pio_sm_set_consecutive_pindirs(PIO_VGA, sm, beginVGA_PIN, 8, true); //конфигурация пинов на выход
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + 0, offset + (pio_program_VGA.length - 1));
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX); //увеличение буфера TX за счёт RX до 8-ми
    sm_config_set_out_shift(&c, true, true, 32);
    sm_config_set_out_pins(&c, beginVGA_PIN, 8);
    pio_sm_init(PIO_VGA, sm, offset, &c);
    pio_sm_set_enabled(PIO_VGA, sm, true);
    //инициализация DMA
    dma_chan_ctrl = dma_claim_unused_channel(true);
    dma_chan = dma_claim_unused_channel(true);
    //основной ДМА канал для данных
    dma_channel_config c0 = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    uint dreq = DREQ_PIO1_TX0 + sm;
    if (PIO_VGA == pio0) dreq = DREQ_PIO0_TX0 + sm;
    channel_config_set_dreq(&c0, dreq);
    channel_config_set_chain_to(&c0, dma_chan_ctrl); // chain to other channel
    dma_channel_configure(
        dma_chan,
        &c0,
        &PIO_VGA->txf[sm], // Write address
        &initial_lines_pattern[0], // read address
        600 / 4, //
        false // Don't start yet
    );
    //канал DMA для контроля основного канала
    dma_channel_config c1 = dma_channel_get_default_config(dma_chan_ctrl);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);
    channel_config_set_read_increment(&c1, false);
    channel_config_set_write_increment(&c1, false);
    channel_config_set_chain_to(&c1, dma_chan); // chain to other channel
    dma_channel_configure(
        dma_chan_ctrl,
        &c1,
        &dma_hw->ch[dma_chan].read_addr, // Write address
        &initial_lines_pattern[0], // read address
        1, //
        false // Don't start yet
    );
    irq_set_exclusive_handler(VGA_DMA_IRQ, dma_handler_VGA);
    dma_channel_set_irq0_enabled(dma_chan_ctrl, true);
    irq_set_enabled(VGA_DMA_IRQ, true);
    dma_start_channel_mask((1u << dma_chan));

    vga_set_bgcolor(0x000000);
    init_palette();
}


void __time_critical_func(render_core)() {
    __dmb();
    _fnc();
    sem_acquire_blocking(&vga_start_semaphore);
    // TODO:
}

void DispHstxCore1Exec(void (*fnc)()) {
    _fnc = fnc;
    sem_init(&vga_start_semaphore, 0, 1);
    multicore_launch_core1(render_core);
}

void DispHstxCore1Wait() {
    sem_release(&vga_start_semaphore);
}
