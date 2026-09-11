#include "tnc155/upd7220.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void gdc_command(tnc155_upd7220 *gdc, uint8_t value)
{
    tnc155_upd7220_write_command(gdc, value);
    tnc155_upd7220_service(gdc, 1u);
}

static void gdc_parameter(tnc155_upd7220 *gdc, uint8_t value)
{
    tnc155_upd7220_write_parameter(gdc, value);
    tnc155_upd7220_service(gdc, 1u);
}

int main(void)
{
    tnc155_upd7220 gdc;
    static const uint8_t reset_parameters[8] =
        {0x14, 0x1e, 0xe3, 0x10, 0x06, 0x0a, 0xd4, 0xd9};
    unsigned i;

    /* The host interface now has a real 16-byte tagged FIFO.  Exercise its
       capacity/status/overrun behavior without command-processor service. */
    {
        tnc155_upd7220 fifo_gdc;
        unsigned j;
        tnc155_upd7220_init(&fifo_gdc);
        assert(tnc155_upd7220_status(&fifo_gdc) == 0x24u);
        for (j = 0u; j < 16u; ++j)
            tnc155_upd7220_write_command(&fifo_gdc, 0x0cu);
        assert(fifo_gdc.fifo_count == 16u);
        assert((tnc155_upd7220_status(&fifo_gdc) & 0x02u) != 0u);
        assert((tnc155_upd7220_status(&fifo_gdc) & 0x04u) == 0u);
        tnc155_upd7220_write_command(&fifo_gdc, 0x0cu);
        assert(fifo_gdc.fifo_overruns == 1u);
        assert(fifo_gdc.fifo_count == 16u);
        tnc155_upd7220_service(&fifo_gdc, 8u);
        assert(fifo_gdc.fifo_count == 8u);
        assert((tnc155_upd7220_status(&fifo_gdc) & 0x02u) == 0u);
        tnc155_upd7220_service(&fifo_gdc, 8u);
        assert(fifo_gdc.fifo_count == 0u);
        assert((tnc155_upd7220_status(&fifo_gdc) & 0x04u) != 0u);
        assert(fifo_gdc.command_count == 16u);
        assert(fifo_gdc.fifo_entries_processed == 16u);
        assert(fifo_gdc.fifo_high_watermark == 16u);
    }

    tnc155_upd7220_init(&gdc);
    assert(tnc155_upd7220_status(&gdc) == 0x24u);
    gdc_command(&gdc, 0x00u);
    for (i = 0; i < sizeof(reset_parameters); ++i)
        gdc_parameter(&gdc, reset_parameters[i]);
    assert(gdc.reset_count == 1u && gdc.parameter_count == 8u);
    assert(gdc.display_mode == TNC155_GDC_MODE_MIXED);
    assert(gdc.active_lines == 468u);

    /* P2's visualization task enters pure graphics with SYNC >0E and
       parameters beginning >16, then controls blanking with >0C/>0D. */
    {
        static const uint8_t graphics_sync[8] =
            {0x16, 0x1e, 0xe3, 0x0c, 0x07, 0x04, 0xea, 0x99};
        gdc_command(&gdc, 0x0eu);
        for (i = 0; i < sizeof(graphics_sync); ++i)
            gdc_parameter(&gdc, graphics_sync[i]);
        assert(gdc.display_mode == TNC155_GDC_MODE_GRAPHICS);
        assert(gdc.active_lines == 490u);
        assert(!gdc.display_enabled);
        gdc_command(&gdc, 0x0du);
        assert(gdc.display_enabled);
        gdc_command(&gdc, 0x0cu);
        assert(!gdc.display_enabled);

        /* Restore the normal P2 text/mixed-mode timing for the remainder. */
        gdc_command(&gdc, 0x0fu);
        for (i = 0; i < sizeof(reset_parameters); ++i)
            gdc_parameter(&gdc, reset_parameters[i]);
        assert(gdc.display_mode == TNC155_GDC_MODE_MIXED);
        assert(gdc.active_lines == 468u);
        assert(gdc.display_enabled);
    }

    gdc_command(&gdc, 0x47u);
    gdc_parameter(&gdc, 0x20u);
    assert(gdc.pitch == 0x20u);
    /* One mixed-mode coded-character partition starting at zero. */
    gdc_command(&gdc, 0x70u);
    gdc_parameter(&gdc, 0x00u);
    gdc_parameter(&gdc, 0x00u);
    gdc_parameter(&gdc, 0x40u);
    gdc_parameter(&gdc, 0x1du);
    gdc_command(&gdc, 0x49u);
    gdc_parameter(&gdc, 0x34u);
    gdc_parameter(&gdc, 0x12u);
    gdc_parameter(&gdc, 0x00u);
    assert(gdc.cursor == 0x1234u);
    gdc_command(&gdc, 0x4au);
    gdc_parameter(&gdc, 0xffu);
    gdc_parameter(&gdc, 0xffu);
    gdc_command(&gdc, 0x4cu);
    gdc_parameter(&gdc, 0x02u);
    gdc_parameter(&gdc, 0x01u);
    gdc_parameter(&gdc, 0x00u);
    gdc_command(&gdc, 0x20u);
    gdc_parameter(&gdc, 0x01u);
    gdc_parameter(&gdc, 0x00u);
    assert(gdc.words_written == 2u);
    assert(gdc.character_video_dram[0x2468u] == 0x00u &&
           gdc.character_video_dram[0x2469u] == 0x01u);
    /* A completed WDAT word must be visible to scanout immediately; waiting
       for the next command incorrectly hides startup text for an entire
       command stream. */
    assert(gdc.scanout_character_video[0x2468u] == 0x00u &&
           gdc.scanout_character_video[0x2469u] == 0x01u);
    gdc_command(&gdc, 0x6bu);
    assert(gdc.display_enabled);
    assert(gdc.scanout_character_video[0x2468u] == 0x00u &&
           gdc.scanout_character_video[0x2469u] == 0x01u);
    assert(gdc.scanout_commits == 1u);

    /* Full-word horizontal WDAT runs use the 32-bit pair backend.  Verify
       forward REPLACE and reverse XOR preserve exact uPD7220-visible state. */
    {
        tnc155_upd7220 run_gdc;
        unsigned j;
        tnc155_upd7220_init(&run_gdc);
        run_gdc.mask = 0xffffu;
        run_gdc.cursor = 0x0100u;
        run_gdc.figure_direction = 2u;
        run_gdc.figure_count = 7u;
        gdc_command(&run_gdc, 0x20u);
        gdc_parameter(&run_gdc, 0x34u);
        gdc_parameter(&run_gdc, 0x12u);
        assert(run_gdc.cursor == 0x0108u);
        assert(run_gdc.words_written == 8u);
        assert(run_gdc.last_vram_word == 0x0107u);
        assert(run_gdc.last_vram_value == 0x1234u);
        for (j = 0x0100u; j <= 0x0107u; ++j) {
            unsigned address = j * 2u;
            assert(run_gdc.character_video_dram[address] == 0x12u);
            assert(run_gdc.character_video_dram[address + 1u] == 0x34u);
            assert(run_gdc.scanout_character_video[address] == 0x12u);
            assert(run_gdc.scanout_character_video[address + 1u] == 0x34u);
        }
        gdc_command(&run_gdc, 0x0cu);
        assert(run_gdc.scanout_commits == 1u);

        run_gdc.cursor = 0x0107u;
        run_gdc.figure_direction = 6u;
        run_gdc.figure_count = 7u;
        gdc_command(&run_gdc, 0x21u);
        gdc_parameter(&run_gdc, 0xffu);
        gdc_parameter(&run_gdc, 0x00u);
        assert(run_gdc.cursor == 0x00ffu);
        assert(run_gdc.words_written == 16u);
        assert(run_gdc.last_vram_word == 0x0100u);
        assert(run_gdc.last_vram_value == 0x12cbu);
        for (j = 0x0100u; j <= 0x0107u; ++j) {
            unsigned address = j * 2u;
            assert(run_gdc.character_video_dram[address] == 0x12u);
            assert(run_gdc.character_video_dram[address + 1u] == 0xcbu);
            assert(run_gdc.scanout_character_video[address] == 0x12u);
            assert(run_gdc.scanout_character_video[address + 1u] == 0xcbu);
        }
    }

    /* FIGS + FIGD: in pure graphics mode a four-dot type-0 figure advances
       the CURS mask across one 16-bit display word. */
    {
        uint32_t before_words;
        memset(gdc.character_video_dram, 0, sizeof(gdc.character_video_dram));
        memset(gdc.scanout_character_video, 0, sizeof(gdc.scanout_character_video));
        gdc.display_mode = TNC155_GDC_MODE_GRAPHICS;
        gdc.pitch = 32u;
        gdc_command(&gdc, 0x78u);
        gdc_parameter(&gdc, 0xffu);
        gdc_parameter(&gdc, 0x00u);
        gdc_command(&gdc, 0x49u);
        gdc_parameter(&gdc, 0x00u);
        gdc_parameter(&gdc, 0x00u);
        gdc_parameter(&gdc, 0x00u);
        gdc_command(&gdc, 0x4cu);
        gdc_parameter(&gdc, 0x02u); /* type 0, DIR=2 */
        gdc_parameter(&gdc, 0x03u); /* DC low: four dots */
        gdc_parameter(&gdc, 0x00u);
        before_words = gdc.words_written;
        gdc_command(&gdc, 0x6cu);
        assert(gdc.figure_draws != 0u);
        assert(gdc.words_written == before_words + 4u);
        assert(gdc.scanout_character_video[0] == 0x00u);
        assert(gdc.scanout_character_video[1] == 0x0fu);
        assert((tnc155_upd7220_status(&gdc) & 0x08u) != 0u);
        assert((tnc155_upd7220_status(&gdc) & 0x08u) == 0u);
    }

    /* GCHRD command is no longer ignored.  Use one graphics-character row
       with a nonzero RA15 pattern and verify that it touches display RAM. */
    {
        unsigned j;
        uint32_t before_words = gdc.words_written;
        gdc_command(&gdc, 0x78u);
        for (j = 0u; j < 8u; ++j)
            gdc_parameter(&gdc, j == 7u ? 0xffu : 0x00u);
        gdc_command(&gdc, 0x49u);
        gdc_parameter(&gdc, 0x10u);
        gdc_parameter(&gdc, 0x00u);
        gdc_parameter(&gdc, 0x00u);
        gdc_command(&gdc, 0x4cu);
        gdc_parameter(&gdc, 0x12u); /* type 2, DIR=2 */
        gdc_parameter(&gdc, 0x00u);
        gdc_parameter(&gdc, 0x00u);
        gdc_command(&gdc, 0x68u);
        assert(gdc.graphics_character_draws != 0u);
        assert(gdc.words_written > before_words);
    }

    puts("uPD7220 reset stream and basic registers: OK");
    return 0;
}
