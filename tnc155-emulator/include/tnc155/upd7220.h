#ifndef TNC155_UPD7220_H
#define TNC155_UPD7220_H

#include <stdbool.h>
#include <stdint.h>

struct tnc155_upd7220;
typedef void (*tnc155_upd7220_native_word_cb)(
    void *opaque, const struct tnc155_upd7220 *gdc,
    uint32_t word, uint16_t value);
typedef void (*tnc155_upd7220_native_invalidate_cb)(
    void *opaque, const struct tnc155_upd7220 *gdc);

typedef struct tnc155_upd7220 {
#ifndef TNC155_FIRMWARE
    /* Desktop/portable backend models the original word-oriented display
       memory and keeps a coherent scanout copy.  The STM32 firmware does not
       allocate either array: its dedicated uPD7220 backend consumes the same
       FIFO/commands but executes visible work directly on the native L8
       framebuffer. */
    _Alignas(4) uint8_t character_video_dram[0x40000];
    _Alignas(4) uint8_t scanout_character_video[0x10000];

    tnc155_upd7220_native_word_cb native_word_cb;
    tnc155_upd7220_native_invalidate_cb native_invalidate_cb;
    void *native_video_opaque;
#endif

    uint8_t status;
    uint8_t read_data;

    /* The host-visible FIFO remains a 16-entry uPD7220 FIFO.  STM32 keeps
       this interface because P2 polls FULL/EMPTY and streams commands and
       parameters through it, even though the internal renderer is native. */
    uint8_t fifo_data[16];
    uint8_t fifo_tag[16];
    uint8_t fifo_head;
    uint8_t fifo_tail;
    uint8_t fifo_count;
    uint8_t fifo_direction;

    uint8_t command;
    uint8_t parameters[16];
    uint8_t parameter_count;
    uint8_t parameter_expected;

    /* Host-visible/programmed display state. */
    uint16_t pitch;
    uint16_t active_words;
    uint16_t active_lines;
    uint8_t hsync_width;
    uint8_t vsync_width;
    uint8_t horizontal_front_porch;
    uint8_t horizontal_back_porch;
    uint8_t vertical_front_porch;
    uint8_t vertical_back_porch;
    uint8_t display_mode;
    uint8_t lines_per_character;
    uint8_t display_zoom;
    uint8_t graphics_character_zoom;

    uint32_t cursor;
    uint16_t mask;
    uint8_t parameter_ram[16];
    uint16_t pattern;
    uint8_t bitmap_mod;

    /* FIGS state.  These are command semantics, not an attempt to model the
       original silicon implementation or its cycle timing. */
    uint8_t figure_type;
    uint8_t figure_direction;
    uint16_t figure_count; /* DC */
    uint16_t figure_d;
    uint16_t figure_d1;
    uint16_t figure_d2;
    uint16_t figure_dm;
    bool figure_graphics_data; /* GD in mixed mode */

    uint8_t wdat_low;
    bool wdat_have_low;
    bool wdat_first_word;
    bool display_enabled;
    bool drawing_in_progress;
    bool vsync_active;
    bool hblank_active;

    uint32_t reset_count;
    uint32_t command_count;
    uint32_t command_histogram[256];
    uint32_t words_written;
    uint32_t scanout_commits;
    uint32_t figure_draws;
    uint32_t graphics_character_draws;
    uint32_t fifo_entries_processed;
    uint32_t fifo_overruns;
    uint32_t fifo_underflows;
    uint8_t fifo_high_watermark;

    /* Last-event observability for ROM-driven diagnostics. */
    uint16_t host_pc;
    uint8_t last_fifo_data;
    uint8_t last_fifo_tag;
    uint32_t last_vram_word;
    uint16_t last_vram_value;
} tnc155_upd7220;

enum {
    TNC155_GDC_MODE_MIXED = 0,
    TNC155_GDC_MODE_GRAPHICS = 1,
    TNC155_GDC_MODE_CHARACTER = 2
};

void tnc155_upd7220_init(tnc155_upd7220 *gdc);
uint8_t tnc155_upd7220_status(tnc155_upd7220 *gdc);
uint8_t tnc155_upd7220_read_data(tnc155_upd7220 *gdc);
void tnc155_upd7220_write_command(tnc155_upd7220 *gdc, uint8_t command);
void tnc155_upd7220_write_parameter(tnc155_upd7220 *gdc, uint8_t value);
#ifndef TNC155_FIRMWARE
void tnc155_upd7220_set_native_video_callbacks(
    tnc155_upd7220 *gdc, tnc155_upd7220_native_word_cb word_cb,
    tnc155_upd7220_native_invalidate_cb invalidate_cb, void *opaque);
#else
/* STM32H7 direct backend: FIFO/command semantics remain uPD7220-compatible,
   while all visible text and graphics operations are executed directly into
   the bound native L8 surface.  The atlas contains the P1 fragment banks
   needed by modes 0 and 3..7; modes 1..2 reuse mode 0 with Y offsets. */
void tnc155_upd7220_bind_l8(tnc155_upd7220 *gdc,
                            uint8_t *framebuffer,
                            unsigned width,
                            unsigned height,
                            unsigned stride,
                            const uint8_t *font_atlas);
#endif
void tnc155_upd7220_service(tnc155_upd7220 *gdc, unsigned max_entries);

#endif
