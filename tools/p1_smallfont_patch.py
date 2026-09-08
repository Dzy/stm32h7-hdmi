from pathlib import Path

path = Path('Core/Src/tnc155_firmware.c')
text = path.read_text()

old = '''static void render_text_partition(const tnc155_upd7220 *gdc,
                                  const tnc155_rom_view *font,
                                  const tnc_l8_partition *part,
                                  unsigned dst_base_y, unsigned line_height,
                                  unsigned effective_pitch)
{
    unsigned cell_y;

    for (cell_y = 0u; cell_y < part->length; cell_y += line_height) {
        unsigned cell_height = line_height;
        uint32_t line_base;
        unsigned word_column;

        if (cell_height > (unsigned)part->length - cell_y)
            cell_height = (unsigned)part->length - cell_y;
        line_base = part->start + (cell_y / line_height) * effective_pitch;

        for (word_column = 0u; word_column < 32u; ++word_column) {
            uint32_t word = (line_base + word_column) & 0x7fffu;
            uint16_t cell = (uint16_t)(
                (uint16_t)gdc->scanout_character_video[word * 2u] << 8 |
                gdc->scanout_character_video[word * 2u + 1u]);
            uint8_t ascii = (uint8_t)cell;
            uint8_t mode_data = (uint8_t)(cell >> 8);
            unsigned p1_mode = (mode_data >> 2) & 0x07u;
            unsigned attr = ((mode_data & 0x02u) != 0u ? 2u : 0u) |
                            ((mode_data & 0x01u) != 0u ? 1u : 0u);
            size_t glyph_base = ((size_t)p1_mode * 64u +
                                 (ascii & 0x3fu)) * 32u;
            unsigned row_in_cell;

            for (row_in_cell = 0u; row_in_cell < cell_height; ++row_in_cell) {
                uint8_t glyph = row_in_cell < 32u ?
                                font->bytes[glyph_base + row_in_cell] : 0u;
                const uint32_t *src = &s_text_row_lut[attr][glyph][0];
                uint32_t *dst = (uint32_t *)(void *)(s_native_frame +
                    (size_t)(dst_base_y + cell_y + row_in_cell) *
                    TNC_NATIVE_WIDTH + word_column * 16u);

                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = src[3];
            }
        }
    }
}
'''

new = '''/* P1 modes 0..2 contain the same 64-glyph small font at different
   vertical positions. ROM comparison gives the common offsets below for
   63/64 glyphs; the two exceptional glyphs are also exact copies of mode 0,
   just at a different Y offset. Therefore modes 0..2 all read the mode-0
   source bank and only alter source-row addressing. */
static int p1_small_font_y_offset(unsigned mode, unsigned character)
{
    if (mode == 0u)
        return 0;
    if (mode == 1u)
        return character == 0x3au ? -3 : -6;
    if (mode == 2u)
        return character == 0x1eu ? -4 : -3;
    return 0;
}

static void copy_text_row(unsigned attr, uint8_t glyph, uint32_t *dst)
{
    const uint32_t *src = &s_text_row_lut[attr][glyph][0];
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

static void render_text_partition(const tnc155_upd7220 *gdc,
                                  const tnc155_rom_view *font,
                                  const tnc_l8_partition *part,
                                  unsigned dst_base_y, unsigned line_height,
                                  unsigned effective_pitch)
{
    unsigned cell_y;

    for (cell_y = 0u; cell_y < part->length; cell_y += line_height) {
        unsigned cell_height = line_height;
        uint32_t line_base;
        unsigned word_column;

        if (cell_height > (unsigned)part->length - cell_y)
            cell_height = (unsigned)part->length - cell_y;
        line_base = part->start + (cell_y / line_height) * effective_pitch;

        for (word_column = 0u; word_column < 32u; ++word_column) {
            uint32_t word = (line_base + word_column) & 0x7fffu;
            uint16_t cell = (uint16_t)(
                (uint16_t)gdc->scanout_character_video[word * 2u] << 8 |
                gdc->scanout_character_video[word * 2u + 1u]);
            uint8_t ascii = (uint8_t)cell;
            uint8_t mode_data = (uint8_t)(cell >> 8);
            unsigned p1_mode = (mode_data >> 2) & 0x07u;
            unsigned character = ascii & 0x3fu;
            unsigned attr = ((mode_data & 0x02u) != 0u ? 2u : 0u) |
                            ((mode_data & 0x01u) != 0u ? 1u : 0u);
            bool inverse = (attr & 1u) != 0u;
            bool small_font = p1_mode <= 2u;
            int y_offset = small_font ?
                           p1_small_font_y_offset(p1_mode, character) : 0;
            size_t glyph_base = ((size_t)(small_font ? 0u : p1_mode) * 64u +
                                 character) * 32u;
            unsigned row_in_cell;

            if (small_font && !inverse) {
                /* Canonical mode-0 small glyphs occupy rows 8..24. Move that
                   one source glyph vertically instead of processing blank
                   rows from three duplicate P1 banks. The staging image is
                   already black, so zero source rows require no stores. */
                int first = 8 + y_offset;
                int last = 24 + y_offset;
                if (first < 0)
                    first = 0;
                if (last >= (int)cell_height)
                    last = (int)cell_height - 1;

                for (; first <= last; ++first) {
                    int source_row = first - y_offset;
                    uint8_t glyph = font->bytes[glyph_base + (unsigned)source_row];
                    uint32_t *dst;
                    if (glyph == 0u)
                        continue;
                    dst = (uint32_t *)(void *)(s_native_frame +
                        (size_t)(dst_base_y + cell_y + (unsigned)first) *
                        TNC_NATIVE_WIDTH + word_column * 16u);
                    copy_text_row(attr, glyph, dst);
                }
                continue;
            }

            for (row_in_cell = 0u; row_in_cell < cell_height; ++row_in_cell) {
                int source_row = (int)row_in_cell - y_offset;
                uint8_t glyph = (source_row >= 0 && source_row < 32) ?
                                font->bytes[glyph_base + (unsigned)source_row] : 0u;
                uint32_t *dst;

                /* Non-inverse large/other glyphs can skip blank rows too,
                   because render_tnc_native() pre-clears the staging image. */
                if (!inverse && glyph == 0u)
                    continue;
                dst = (uint32_t *)(void *)(s_native_frame +
                    (size_t)(dst_base_y + cell_y + row_in_cell) *
                    TNC_NATIVE_WIDTH + word_column * 16u);
                copy_text_row(attr, glyph, dst);
            }
        }
    }
}
'''

count = text.count(old)
if count != 1:
    raise SystemExit(f'expected one render_text_partition block, found {count}')
path.write_text(text.replace(old, new))
