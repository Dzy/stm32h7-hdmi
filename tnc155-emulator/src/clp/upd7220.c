#include "tnc155/upd7220.h"

#include <string.h>

#define GDC_SR_DATA_READY          0x01u
#define GDC_SR_FIFO_FULL           0x02u
#define GDC_SR_FIFO_EMPTY          0x04u
#define GDC_SR_DRAWING_IN_PROGRESS 0x08u
#define GDC_SR_DMA_EXECUTE         0x10u
#define GDC_SR_VSYNC_ACTIVE        0x20u
#define GDC_SR_HBLANK_ACTIVE       0x40u

#define GDC_FIFO_CAPACITY          16u
#define GDC_FIFO_TAG_PARAMETER     0u
#define GDC_FIFO_TAG_COMMAND       1u
#define GDC_FIFO_DIRECTION_READ    0u
#define GDC_FIFO_DIRECTION_WRITE   1u

static void native_video_invalidate(tnc155_upd7220 *gdc)
{
    if (gdc->native_invalidate_cb != NULL)
        gdc->native_invalidate_cb(gdc->native_video_opaque, gdc);
}

static uint8_t decode_display_mode(uint8_t sync_mode)
{
    /* uPD7220 display-mode selection is encoded by SYNC/RESET parameter 1
       bits 5 and 1: 00=mixed, 02=graphics, 20=character, 22=invalid. */
    switch (sync_mode & 0x22u) {
    case 0x00u: return TNC155_GDC_MODE_MIXED;
    case 0x02u: return TNC155_GDC_MODE_GRAPHICS;
    case 0x20u: return TNC155_GDC_MODE_CHARACTER;
    default:    return TNC155_GDC_MODE_CHARACTER;
    }
}

static uint8_t parameter_count(uint8_t command)
{
    switch (command) {
    case 0x00u: return 8u;  /* RESET followed by SYNC parameters */
    case 0x46u: return 1u;  /* ZOOM */
    case 0x47u: return 1u;  /* PITCH */
    case 0x49u: return 3u;  /* CURS */
    case 0x4au: return 2u;  /* MASK */
    case 0x4bu: return 3u;  /* CCHAR, only first byte is currently consumed */
    case 0x4cu: return 11u; /* FIGS: DIR/TYPE, DC, D, D2, D1, DM */
    default:
        if ((command & 0xfeu) == 0x0eu) return 8u; /* SYNC */
        if ((command & 0xf0u) == 0x70u) return 16u - (command & 15u); /* PRAM */
        return 0u;
    }
}

static uint16_t read_display_word(const tnc155_upd7220 *gdc, uint32_t word)
{
    /* The decoded TNC155 path currently exposes the first 32K x 16 display
       bank.  Keep that proven wiring unchanged while the remaining IC31.6
       bank-selection logic is still being decoded. */
    uint32_t address = (word & 0x7fffu) * 2u;
    return (uint16_t)((uint16_t)gdc->character_video_dram[address] << 8 |
                      gdc->character_video_dram[address + 1u]);
}

static void publish_display_word(tnc155_upd7220 *gdc, uint32_t word,
                                 uint16_t value)
{
    uint32_t address = (word & 0x7fffu) * 2u;
    gdc->character_video_dram[address] = (uint8_t)(value >> 8);
    gdc->character_video_dram[address + 1u] = (uint8_t)value;

    /* Keep the canonical scanout copy coherent even when the STM32 native
       backend is active. Native invalidation rebuilds the complete surface
       from scanout_character_video; leaving it stale erased early boot text
       when SYNC/PRAM/display changes forced a rebuild. */
    gdc->scanout_character_video[address] = (uint8_t)(value >> 8);
    gdc->scanout_character_video[address + 1u] = (uint8_t)value;

    if (gdc->native_word_cb != NULL)
        gdc->native_word_cb(gdc->native_video_opaque, gdc, word, value);
    gdc->last_vram_word = word & 0x3ffffu;
    gdc->last_vram_value = value;
    ++gdc->words_written;
}

/* The emulated uPD7220 remains 16-bit-word oriented, but the STM32H7 has a
   native 32-bit memory path.  These helpers operate on the byte-exact DRAM
   representation with memcpy so they are alignment/aliasing safe in C while
   GCC can still lower aligned calls to one 32-bit load/store on Cortex-M7. */
static uint32_t load_native32(const uint8_t *src)
{
    uint32_t value;
    memcpy(&value, src, sizeof(value));
    return value;
}

static void store_native32(uint8_t *dst, uint32_t value)
{
    memcpy(dst, &value, sizeof(value));
}

static uint32_t repeated_be16_native(uint16_t value)
{
    uint8_t bytes[4];
    uint32_t native;
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
    bytes[2] = bytes[0];
    bytes[3] = bytes[1];
    memcpy(&native, bytes, sizeof(native));
    return native;
}

static uint32_t apply_mod32(uint32_t current, uint32_t data, uint8_t mod)
{
    switch (mod & 3u) {
    case 0u: return data;
    case 1u: return current ^ data;
    case 2u: return current & ~data;
    default: return current | data;
    }
}

static void publish_display_pair(tnc155_upd7220 *gdc, uint32_t even_word,
                                 uint32_t native_value)
{
    uint32_t address = (even_word & 0x7fffu) * 2u;
    store_native32(&gdc->character_video_dram[address], native_value);
    store_native32(&gdc->scanout_character_video[address], native_value);
    if (gdc->native_word_cb != NULL) {
        gdc->native_word_cb(gdc->native_video_opaque, gdc, even_word,
                            read_display_word(gdc, even_word));
        gdc->native_word_cb(gdc->native_video_opaque, gdc, even_word + 1u,
                            read_display_word(gdc, even_word + 1u));
    }
    gdc->words_written += 2u;
}

static uint16_t rotate_left16(uint16_t value)
{
    return (uint16_t)((uint16_t)(value << 1) | (value >> 15));
}

static uint16_t rotate_right16(uint16_t value)
{
    return (uint16_t)((value >> 1) | (uint16_t)(value << 15));
}

static uint16_t drawing_pitch(const tnc155_upd7220 *gdc)
{
    if (gdc->display_mode == TNC155_GDC_MODE_MIXED &&
        gdc->figure_graphics_data)
        return (uint16_t)(gdc->pitch >> 1);
    return gdc->pitch;
}

static uint16_t drawing_pattern(const tnc155_upd7220 *gdc, unsigned cycle)
{
    if (gdc->display_mode == TNC155_GDC_MODE_GRAPHICS ||
        (gdc->display_mode == TNC155_GDC_MODE_MIXED &&
         gdc->figure_graphics_data))
        return ((gdc->pattern >> (cycle & 15u)) & 1u) ? 0xffffu : 0u;
    return gdc->pattern;
}

static void write_vram(tnc155_upd7220 *gdc, uint8_t type, uint8_t mod,
                       uint16_t data, uint16_t mask)
{
    uint16_t effective_mask = mask;
    uint16_t effective_data = data;
    uint16_t current;
    uint16_t result;

    if (type == 2u) {
        effective_mask &= 0x00ffu;
        effective_data &= 0x00ffu;
    } else if (type == 3u) {
        effective_mask &= 0xff00u;
        effective_data &= 0xff00u;
    } else if (type == 1u) {
        return;
    }

    effective_data &= effective_mask;
    /* Full-word REPLACE is the dominant bulk-fill case and does not need the
       old VRAM value at all.  Avoid an external-memory read before writing. */
    if (type == 0u && effective_mask == 0xffffu && (mod & 3u) == 0u) {
        publish_display_word(gdc, gdc->cursor, effective_data);
        return;
    }

    current = read_display_word(gdc, gdc->cursor);
    switch (mod & 3u) {
    case 0u:
        result = (uint16_t)((current & (uint16_t)~effective_mask) |
                            effective_data);
        break;
    case 1u:
        result = (uint16_t)(current ^ effective_data);
        break;
    case 2u:
        result = (uint16_t)(current & (uint16_t)~effective_data);
        break;
    default:
        result = (uint16_t)(current | effective_data);
        break;
    }
    publish_display_word(gdc, gdc->cursor, result);
}

static void next_pixel(tnc155_upd7220 *gdc, unsigned direction)
{
    uint16_t pitch = drawing_pitch(gdc);
    switch (direction & 7u) {
    case 0u:
        gdc->cursor += pitch;
        break;
    case 1u:
        gdc->cursor += pitch;
        if ((gdc->mask & 0x8000u) != 0u)
            ++gdc->cursor;
        gdc->mask = rotate_left16(gdc->mask);
        break;
    case 2u:
        if ((gdc->mask & 0x8000u) != 0u)
            ++gdc->cursor;
        gdc->mask = rotate_left16(gdc->mask);
        break;
    case 3u:
        gdc->cursor -= pitch;
        if ((gdc->mask & 0x8000u) != 0u)
            ++gdc->cursor;
        gdc->mask = rotate_left16(gdc->mask);
        break;
    case 4u:
        gdc->cursor -= pitch;
        break;
    case 5u:
        gdc->cursor -= pitch;
        if ((gdc->mask & 1u) != 0u)
            --gdc->cursor;
        gdc->mask = rotate_right16(gdc->mask);
        break;
    case 6u:
        if ((gdc->mask & 1u) != 0u)
            --gdc->cursor;
        gdc->mask = rotate_right16(gdc->mask);
        break;
    default:
        gdc->cursor += pitch;
        if ((gdc->mask & 1u) != 0u)
            --gdc->cursor;
        gdc->mask = rotate_right16(gdc->mask);
        break;
    }
    gdc->cursor &= 0x3ffffu;
}

static int sign_extend14(uint16_t value)
{
    value &= 0x3fffu;
    if ((value & 0x2000u) != 0u)
        return (int)value - 0x4000;
    return (int)value;
}

static void reset_figure_parameters(tnc155_upd7220 *gdc)
{
    gdc->figure_count = 0u;
    gdc->figure_d = 8u;
    gdc->figure_d1 = 0xffffu;
    gdc->figure_d2 = 8u;
    gdc->figure_dm = 0xffffu;
    gdc->figure_graphics_data = false;
    gdc->figure_type = 0u;
    gdc->pattern = (uint16_t)(gdc->parameter_ram[8] |
                              ((uint16_t)gdc->parameter_ram[9] << 8));
}

static void draw_pixel(tnc155_upd7220 *gdc)
{
    unsigned i;
    for (i = 0u; i <= gdc->figure_count; ++i) {
        write_vram(gdc, 0u, gdc->bitmap_mod,
                   drawing_pattern(gdc, i), gdc->mask);
        next_pixel(gdc, gdc->figure_direction);
    }
}

static void draw_line(tnc155_upd7220 *gdc)
{
    int d = sign_extend14(gdc->figure_d);
    int d1 = sign_extend14(gdc->figure_d1);
    int d2 = sign_extend14(gdc->figure_d2);
    uint8_t octant = gdc->figure_direction;
    unsigned i;

    for (i = 0u; i <= gdc->figure_count; ++i) {
        write_vram(gdc, 0u, gdc->bitmap_mod,
                   drawing_pattern(gdc, i), gdc->mask);
        if ((octant & 1u) != 0u)
            gdc->figure_direction = (uint8_t)(d < 0 ?
                ((octant + 1u) & 7u) : octant);
        else
            gdc->figure_direction = (uint8_t)(d < 0 ?
                octant : ((octant + 1u) & 7u));
        d += d < 0 ? d1 : d2;
        next_pixel(gdc, gdc->figure_direction);
    }
}

static void draw_arc(tnc155_upd7220 *gdc)
{
    int err = -(int)gdc->figure_d;
    int d = (int)gdc->figure_d + 1;
    uint8_t octant = gdc->figure_direction;
    unsigned i;

    for (i = 0u; i <= gdc->figure_count; ++i) {
        if (i >= gdc->figure_dm)
            write_vram(gdc, 0u, gdc->bitmap_mod,
                       drawing_pattern(gdc, i & 15u), gdc->mask);
        if (err < 0)
            gdc->figure_direction = (uint8_t)((octant & 1u) != 0u ?
                ((octant + 1u) & 7u) : octant);
        else
            gdc->figure_direction = (uint8_t)((octant & 1u) != 0u ?
                octant : ((octant + 1u) & 7u));
        if (err < 0)
            err += ((int)i + 1) << 1;
        else {
            --d;
            err += ((int)i - d + 1) << 1;
        }
        next_pixel(gdc, gdc->figure_direction);
    }
}

static void draw_rectangle(tnc155_upd7220 *gdc)
{
    unsigned side;
    for (side = 0u; side < 4u; ++side) {
        uint16_t distance = (side & 1u) != 0u ?
                            gdc->figure_d2 : gdc->figure_d;
        uint16_t j;
        for (j = 0u; j < distance; ++j) {
            write_vram(gdc, 0u, gdc->bitmap_mod,
                       drawing_pattern(gdc, j & 15u), gdc->mask);
            if (side > 0u && j == 0u)
                gdc->figure_direction =
                    (uint8_t)((gdc->figure_direction + 2u) & 7u);
            next_pixel(gdc, gdc->figure_direction);
        }
    }
}

static void draw_graphics_character(tnc155_upd7220 *gdc)
{
    static const int8_t direction_change[2][4] = {
        { 2, 2, -2, -2 },
        { 1, 3, -3, -1 }
    };
    unsigned type = (gdc->figure_type & 0x10u) >> 4;
    unsigned i;
    unsigned di = 0u;

    for (i = 0u; i <= gdc->figure_count; ++i) {
        unsigned zoom_y;
        uint8_t row = gdc->parameter_ram[15u - (i & 7u)];
        gdc->pattern = (uint16_t)((uint16_t)row << 8 | row);
        for (zoom_y = 0u; zoom_y <= gdc->graphics_character_zoom;
             ++zoom_y, ++di) {
            unsigned j;
            for (j = 0u; j < gdc->figure_d; ++j) {
                unsigned cycle = (di & 1u) != 0u ?
                                 15u - (j & 15u) : (j & 15u);
                unsigned zoom_x;
                uint16_t pattern = drawing_pattern(gdc, cycle);
                for (zoom_x = 0u; zoom_x <= gdc->graphics_character_zoom;
                     ++zoom_x) {
                    write_vram(gdc, 0u, gdc->bitmap_mod,
                               pattern, gdc->mask);
                    if (j + 1u != gdc->figure_d ||
                        zoom_x != gdc->graphics_character_zoom)
                        next_pixel(gdc, gdc->figure_direction);
                }
            }
            gdc->figure_direction = (uint8_t)(
                ((int)gdc->figure_direction +
                 direction_change[type][(di & 1u) << 1]) & 7);
            next_pixel(gdc, gdc->figure_direction);
            gdc->figure_direction = (uint8_t)(
                ((int)gdc->figure_direction +
                 direction_change[type][((di & 1u) << 1) + 1u]) & 7);
        }
    }
}

static void execute_figure_draw(tnc155_upd7220 *gdc)
{
    switch (gdc->figure_type) {
    case 0u: draw_pixel(gdc); break;
    case 1u: draw_line(gdc); break;
    case 4u: draw_arc(gdc); break;
    case 8u: draw_rectangle(gdc); break;
    default: break;
    }
    ++gdc->figure_draws;
    gdc->drawing_in_progress = true;
    reset_figure_parameters(gdc);
}

static void execute_graphics_character_draw(tnc155_upd7220 *gdc)
{
    if ((gdc->figure_type & 0x0fu) == 2u)
        draw_graphics_character(gdc);
    ++gdc->graphics_character_draws;
    gdc->drawing_in_progress = true;
    reset_figure_parameters(gdc);
}

static uint32_t wdat_next_cursor(const tnc155_upd7220 *gdc, uint32_t cursor)
{
    static const int8_t x_dir[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int8_t y_dir[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    unsigned dir = gdc->figure_direction & 7u;
    int32_t next = (int32_t)cursor + x_dir[dir] +
                   y_dir[dir] * (int32_t)drawing_pitch(gdc);
    return (uint32_t)next & 0x3ffffu;
}

static void write_wdat_horizontal_run(tnc155_upd7220 *gdc, uint8_t mod,
                                      uint16_t data, uint32_t repeats,
                                      int step)
{
    uint32_t cursor = gdc->cursor;
    uint32_t data_pair = repeated_be16_native(data);
    unsigned pair_parity = step > 0 ? 0u : 1u;

    /* Align the first pair.  A forward pair starts on an even GDC word;
       a reverse pair starts on the odd word and covers odd,even in that
       execution order.  Either case maps to one aligned native 32-bit word. */
    if (repeats != 0u && (cursor & 1u) != pair_parity) {
        gdc->cursor = cursor;
        write_vram(gdc, 0u, mod, data, 0xffffu);
        cursor = (uint32_t)((int32_t)cursor + step) & 0x3ffffu;
        --repeats;
    }

    while (repeats >= 2u) {
        uint32_t even_word = step > 0 ? cursor :
                             (uint32_t)(cursor - 1u) & 0x3ffffu;
        uint32_t address = (even_word & 0x7fffu) * 2u;
        uint32_t result;

        if ((mod & 3u) == 0u) {
            result = data_pair;
        } else {
            uint32_t current = load_native32(
                &gdc->character_video_dram[address]);
            result = apply_mod32(current, data_pair, mod);
        }
        publish_display_pair(gdc, even_word, result);
        cursor = (uint32_t)((int32_t)cursor + step * 2) & 0x3ffffu;
        repeats -= 2u;
    }

    if (repeats != 0u) {
        gdc->cursor = cursor;
        write_vram(gdc, 0u, mod, data, 0xffffu);
        cursor = (uint32_t)((int32_t)cursor + step) & 0x3ffffu;
    }

    gdc->cursor = cursor;
    /* The paired stores bypass publish_display_word(), so restore the same
       last-event observability after the complete logical WDAT run. */
    gdc->last_vram_word =
        (uint32_t)((int32_t)cursor - step) & 0x3ffffu;
    gdc->last_vram_value = read_display_word(gdc, gdc->last_vram_word);
}

static void write_wdat_word(tnc155_upd7220 *gdc, uint16_t supplied)
{
    uint8_t type = (uint8_t)((gdc->command & 0x18u) >> 3);
    uint8_t mod = (uint8_t)(gdc->command & 3u);
    uint32_t repeats = gdc->wdat_first_word ?
                       (uint32_t)gdc->figure_count + 1u : 1u;
    uint16_t data = supplied;
    unsigned dir = gdc->figure_direction & 7u;

    gdc->pattern = supplied;

    /* Fast bulk path: full-word horizontal WDAT fills are naturally adjacent
       in VRAM.  Process two emulated 16-bit GDC words per native 32-bit
       transaction.  This deliberately optimizes implementation bandwidth,
       not uPD7220 timing; all visible cursor/MOD/word-count semantics remain. */
    if (type == 0u && gdc->mask == 0xffffu && repeats >= 2u &&
        (dir == 2u || dir == 6u)) {
        write_wdat_horizontal_run(gdc, mod, data, repeats,
                                  dir == 2u ? 1 : -1);
    } else {
        while (repeats-- != 0u) {
            write_vram(gdc, type, mod, data, gdc->mask);
            /* WDAT address stepping follows the uPD7220 FIGS direction
               vectors; unlike FIGD pixel stepping it does not rotate mask. */
            gdc->cursor = wdat_next_cursor(gdc, gdc->cursor);
        }
    }
    gdc->wdat_first_word = false;
}

static void fifo_clear(tnc155_upd7220 *gdc)
{
    gdc->fifo_head = 0u;
    gdc->fifo_tail = 0u;
    gdc->fifo_count = 0u;
}

static void fifo_set_direction(tnc155_upd7220 *gdc, uint8_t direction)
{
    if (gdc->fifo_direction != direction) {
        fifo_clear(gdc);
        gdc->fifo_direction = direction;
    }
}

static bool fifo_push(tnc155_upd7220 *gdc, uint8_t data, uint8_t tag)
{
    if (gdc->fifo_count >= GDC_FIFO_CAPACITY) {
        ++gdc->fifo_overruns;
        return false;
    }

    gdc->fifo_data[gdc->fifo_tail] = data;
    gdc->fifo_tag[gdc->fifo_tail] = tag;
    gdc->fifo_tail = (uint8_t)((gdc->fifo_tail + 1u) & 15u);
    ++gdc->fifo_count;
    if (gdc->fifo_count > gdc->fifo_high_watermark)
        gdc->fifo_high_watermark = gdc->fifo_count;
    return true;
}

static bool fifo_pop(tnc155_upd7220 *gdc, uint8_t *data, uint8_t *tag)
{
    if (gdc->fifo_count == 0u)
        return false;

    *data = gdc->fifo_data[gdc->fifo_head];
    *tag = gdc->fifo_tag[gdc->fifo_head];
    gdc->fifo_head = (uint8_t)((gdc->fifo_head + 1u) & 15u);
    --gdc->fifo_count;
    return true;
}

void tnc155_upd7220_init(tnc155_upd7220 *gdc)
{
    memset(gdc, 0, sizeof(*gdc));
    /* The host FIFO is functional, but its consumption rate is deliberately
       not tied to an invented uPD7220 clock.  The machine scheduler advances
       the command processor independently with tnc155_upd7220_service().
       VSYNC remains asserted until the board timer frequency is decoded. */
    gdc->fifo_direction = GDC_FIFO_DIRECTION_WRITE;
    fifo_clear(gdc);
    gdc->status = GDC_SR_FIFO_EMPTY;
    gdc->vsync_active = true;
    gdc->mask = 0xffffu;
    gdc->lines_per_character = 1u;
    reset_figure_parameters(gdc);
}

uint8_t tnc155_upd7220_status(tnc155_upd7220 *gdc)
{
    uint8_t result = 0u;
    if (gdc->fifo_count == 0u)
        result |= GDC_SR_FIFO_EMPTY;
    if (gdc->fifo_count >= GDC_FIFO_CAPACITY)
        result |= GDC_SR_FIFO_FULL;
    if (gdc->fifo_direction == GDC_FIFO_DIRECTION_READ &&
        gdc->fifo_count != 0u)
        result |= GDC_SR_DATA_READY;
    if (gdc->drawing_in_progress)
        result |= GDC_SR_DRAWING_IN_PROGRESS;
    if (gdc->vsync_active)
        result |= GDC_SR_VSYNC_ACTIVE;
    if (gdc->hblank_active)
        result |= GDC_SR_HBLANK_ACTIVE;
    gdc->status = result;
    /* FIGD/GCHRD are executed synchronously, so by the next status sample the
       engine is no longer busy.  Returning the bit once still preserves the
       visible command transition for firmware that polls it. */
    gdc->drawing_in_progress = false;
    return result;
}

uint8_t tnc155_upd7220_read_data(tnc155_upd7220 *gdc)
{
    uint8_t data;
    uint8_t tag;

    fifo_set_direction(gdc, GDC_FIFO_DIRECTION_READ);
    if (fifo_pop(gdc, &data, &tag)) {
        (void)tag;
        gdc->read_data = data;
        return data;
    }

    ++gdc->fifo_underflows;
    return gdc->read_data;
}

static void process_command(tnc155_upd7220 *gdc, uint8_t command)
{
    /* A command terminates the preceding variable-length WDAT data stream.
       Every completed word is already published coherently to both backing
       and scanout RAM, so a 64 KiB memcpy here is redundant.  Keep the commit
       counter as an observable stream-boundary event without copying data. */
    if ((gdc->command & 0xe4u) == 0x20u && gdc->parameter_count != 0u)
        ++gdc->scanout_commits;
    ++gdc->command_count;
    ++gdc->command_histogram[command];
    gdc->command = command;
    gdc->parameter_count = 0u;
    gdc->parameter_expected = parameter_count(command);

    if (command == 0x00u) {
        ++gdc->reset_count;
        /* RESET clears the uPD7220 host FIFO/command pipeline.  In the TNC
           path the scheduler normally consumes RESET before P2 supplies the
           eight following timing bytes, matching the hardware handshake. */
        fifo_clear(gdc);
        gdc->display_enabled = false;
        gdc->cursor = 0u;
        gdc->drawing_in_progress = false;
        reset_figure_parameters(gdc);
    } else if ((command & 0xfeu) == 0x0eu) {
        /* SYNC bit 0 controls display enable while the following eight
           parameters reprogram the same mode/timing fields as RESET. */
        gdc->display_enabled = (command & 1u) != 0u;
    } else if ((command & 0xfeu) == 0x0cu) {
        /* BCTRL: P2 uses >0C/>0D to blank/unblank around graphics setup and
           page changes. */
        gdc->display_enabled = (command & 1u) != 0u;
    } else if (command == 0x6bu) {
        gdc->display_enabled = true;
        if (gdc->native_word_cb == NULL)
            memcpy(gdc->scanout_character_video, gdc->character_video_dram,
                   sizeof(gdc->scanout_character_video));
    } else if (command == 0x6au) {
        gdc->display_enabled = false;
    } else if (command == 0x6cu) {
        execute_figure_draw(gdc);
    } else if (command == 0x68u) {
        execute_graphics_character_draw(gdc);
    }

    if ((command & 0xe4u) == 0x20u) {
        gdc->bitmap_mod = (uint8_t)(command & 3u);
        gdc->wdat_have_low = false;
        gdc->wdat_first_word = true;
    }

    if (command == 0x00u || (command & 0xfeu) == 0x0eu ||
        (command & 0xfeu) == 0x0cu || command == 0x6bu || command == 0x6au)
        native_video_invalidate(gdc);
}

static void decode_sync_parameter(tnc155_upd7220 *gdc, unsigned index,
                                  uint8_t value)
{
    if (index == 0u) {
        gdc->display_mode = decode_display_mode(value);
    } else if (index == 1u) {
        gdc->active_words = (uint16_t)value + 2u;
        gdc->pitch = gdc->active_words;
    } else if (index == 2u) {
        gdc->hsync_width = (uint8_t)((value & 0x1fu) + 1u);
    } else if (index == 3u) {
        gdc->vsync_width = (uint8_t)(((value & 3u) << 3) |
                                     (gdc->parameters[2] >> 5));
        gdc->horizontal_front_porch = (uint8_t)((value >> 2) + 1u);
    } else if (index == 4u) {
        gdc->horizontal_back_porch = (uint8_t)((value & 0x3fu) + 1u);
    } else if (index == 5u) {
        gdc->vertical_front_porch = (uint8_t)(value & 0x3fu);
    } else if (index == 7u) {
        gdc->active_lines = (uint16_t)(gdc->parameters[6] |
                            ((uint16_t)(value & 3u) << 8));
        gdc->vertical_back_porch = (uint8_t)(value >> 2);
    }
}

static void process_parameter(tnc155_upd7220 *gdc, uint8_t value)
{
    unsigned index = gdc->parameter_count;
    if (index < sizeof(gdc->parameters))
        gdc->parameters[index] = value;
    if (gdc->parameter_count != 0xffu)
        ++gdc->parameter_count;

    if (gdc->command == 0x47u && index == 0u) {
        gdc->pitch = value;
    } else if (gdc->command == 0x46u && index == 0u) {
        gdc->graphics_character_zoom = (uint8_t)(value & 0x0fu);
        gdc->display_zoom = (uint8_t)(value >> 4);
    } else if (gdc->command == 0x00u ||
               (gdc->command & 0xfeu) == 0x0eu) {
        decode_sync_parameter(gdc, index, value);
    } else if (gdc->command == 0x4bu && index == 0u) {
        gdc->lines_per_character = (uint8_t)((value & 0x1fu) + 1u);
    } else if (gdc->command == 0x49u && index == 2u) {
        gdc->cursor = (uint32_t)gdc->parameters[0] |
                      ((uint32_t)gdc->parameters[1] << 8) |
                      ((uint32_t)(value & 3u) << 16);
        gdc->mask = (uint16_t)(1u << ((value >> 4) & 15u));
    } else if (gdc->command == 0x4au && index == 1u) {
        gdc->mask = (uint16_t)(gdc->parameters[0] |
                              ((uint16_t)gdc->parameters[1] << 8));
    } else if (gdc->command == 0x4cu) {
        if (index == 0u) {
            gdc->figure_type = (uint8_t)((value & 0xf8u) >> 3);
            gdc->figure_direction = (uint8_t)(value & 7u);
        } else if (index == 1u) {
            gdc->figure_count = (uint16_t)((gdc->figure_count & 0x3f00u) |
                                           value);
        } else if (index == 2u) {
            gdc->figure_count = (uint16_t)(gdc->parameters[1] |
                ((uint16_t)(value & 0x3fu) << 8));
            gdc->figure_graphics_data =
                (value & 0x40u) != 0u &&
                gdc->display_mode == TNC155_GDC_MODE_MIXED;
        } else if (index == 4u) {
            gdc->figure_d = (uint16_t)(gdc->parameters[3] |
                ((uint16_t)(value & 0x3fu) << 8));
        } else if (index == 6u) {
            gdc->figure_d2 = (uint16_t)(gdc->parameters[5] |
                ((uint16_t)(value & 0x3fu) << 8));
        } else if (index == 8u) {
            gdc->figure_d1 = (uint16_t)(gdc->parameters[7] |
                ((uint16_t)(value & 0x3fu) << 8));
        } else if (index == 10u) {
            gdc->figure_dm = (uint16_t)(gdc->parameters[9] |
                ((uint16_t)(value & 0x3fu) << 8));
        }
    } else if ((gdc->command & 0xf0u) == 0x70u) {
        unsigned ram_address = (gdc->command & 15u) + index;
        if (ram_address < sizeof(gdc->parameter_ram)) {
            gdc->parameter_ram[ram_address] = value;
            if (ram_address == 8u)
                gdc->pattern = (uint16_t)((gdc->pattern & 0xff00u) | value);
            else if (ram_address == 9u)
                gdc->pattern = (uint16_t)((gdc->pattern & 0x00ffu) |
                                          ((uint16_t)value << 8));
        }
    } else if ((gdc->command & 0xe4u) == 0x20u) {
        uint8_t type = (uint8_t)((gdc->command & 0x18u) >> 3);
        bool byte_transfer = type == 2u || type == 3u;
        if (byte_transfer) {
            uint16_t supplied = type == 3u ? (uint16_t)value << 8 : value;
            write_wdat_word(gdc, supplied);
        } else if (!gdc->wdat_have_low) {
            gdc->wdat_low = value;
            gdc->wdat_have_low = true;
        } else {
            uint16_t supplied = (uint16_t)(gdc->wdat_low |
                                           ((uint16_t)value << 8));
            write_wdat_word(gdc, supplied);
            gdc->wdat_have_low = false;
        }
    }

    if ((gdc->command == 0x47u && index == 0u) ||
        gdc->command == 0x00u || (gdc->command & 0xfeu) == 0x0eu ||
        (gdc->command == 0x4bu && index == 0u) ||
        (gdc->command & 0xf0u) == 0x70u)
        native_video_invalidate(gdc);
}

void tnc155_upd7220_write_command(tnc155_upd7220 *gdc, uint8_t command)
{
    fifo_set_direction(gdc, GDC_FIFO_DIRECTION_WRITE);
    (void)fifo_push(gdc, command, GDC_FIFO_TAG_COMMAND);
}

void tnc155_upd7220_write_parameter(tnc155_upd7220 *gdc, uint8_t value)
{
    fifo_set_direction(gdc, GDC_FIFO_DIRECTION_WRITE);
    (void)fifo_push(gdc, value, GDC_FIFO_TAG_PARAMETER);
}

void tnc155_upd7220_set_native_video_callbacks(
    tnc155_upd7220 *gdc, tnc155_upd7220_native_word_cb word_cb,
    tnc155_upd7220_native_invalidate_cb invalidate_cb, void *opaque)
{
    if (gdc == NULL)
        return;
    gdc->native_word_cb = word_cb;
    gdc->native_invalidate_cb = invalidate_cb;
    gdc->native_video_opaque = opaque;
    native_video_invalidate(gdc);
}

void tnc155_upd7220_service(tnc155_upd7220 *gdc, unsigned max_entries)
{
    uint8_t data;
    uint8_t tag;

    if (gdc == NULL || max_entries == 0u ||
        gdc->fifo_direction != GDC_FIFO_DIRECTION_WRITE)
        return;

    while (max_entries-- != 0u && fifo_pop(gdc, &data, &tag)) {
        gdc->last_fifo_data = data;
        gdc->last_fifo_tag = tag;
        if (tag == GDC_FIFO_TAG_COMMAND)
            process_command(gdc, data);
        else
            process_parameter(gdc, data);
        ++gdc->fifo_entries_processed;
    }
}
