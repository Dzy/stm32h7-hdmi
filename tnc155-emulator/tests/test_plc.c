#include "tnc155/plc.h"
#include "tnc155/roms.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    tnc155_plc plc;
    tnc155_plc_reset(&plc);

    /* U M1; UN M2; = M3 */
    tnc155_plc_write_bit(&plc, 1u, true);
    tnc155_plc_write_bit(&plc, 2u, false);
    assert(tnc155_plc_step_word(&plc, 0x1001u) == TNC155_PLC_STEP_OK);
    assert(tnc155_plc_step_word(&plc, 0x2002u) == TNC155_PLC_STEP_OK);
    assert(tnc155_plc_step_word(&plc, 0xb003u) == TNC155_PLC_STEP_OK);
    assert(tnc155_plc_read_bit(&plc, 3u));
    assert(!plc.chain_active);

    /* A new O sequence starts from false. */
    tnc155_plc_write_bit(&plc, 4u, false);
    tnc155_plc_write_bit(&plc, 5u, true);
    assert(tnc155_plc_step_word(&plc, 0x3004u) == TNC155_PLC_STEP_OK);
    assert(tnc155_plc_step_word(&plc, 0x3005u) == TNC155_PLC_STEP_OK);
    assert(tnc155_plc_step_word(&plc, 0xb006u) == TNC155_PLC_STEP_OK);
    assert(tnc155_plc_read_bit(&plc, 6u));

    /* Conditional set/reset variants use the completed logic result. */
    assert(tnc155_plc_step_word(&plc, 0x1001u) == TNC155_PLC_STEP_OK);
    assert(tnc155_plc_step_word(&plc, 0x7007u) == TNC155_PLC_STEP_OK);
    assert(tnc155_plc_read_bit(&plc, 7u));
    assert(tnc155_plc_step_word(&plc, 0x1002u) == TNC155_PLC_STEP_OK);
    assert(tnc155_plc_step_word(&plc, 0x8008u) == TNC155_PLC_STEP_OK);
    assert(tnc155_plc_read_bit(&plc, 8u));
    assert(tnc155_plc_step_word(&plc, 0x1001u) == TNC155_PLC_STEP_OK);
    assert(tnc155_plc_step_word(&plc, 0x9007u) == TNC155_PLC_STEP_OK);
    assert(!tnc155_plc_read_bit(&plc, 7u));

    assert(tnc155_plc_step_word(&plc, 0xc123u) ==
           TNC155_PLC_STEP_RESERVED_OPCODE);
    assert(plc.reserved_opcodes == 1u);

    tnc155_plc_write_marker(&plc, 2176u, true);
    assert(tnc155_plc_read_marker(&plc, 2176u));
    tnc155_plc_set_input(&plc, 127u, true);
    assert(tnc155_plc_read_input(&plc, 127u));
    tnc155_plc_write_bit(&plc, TNC155_PLC_OUTPUT_BASE + 63u, true);
    assert(tnc155_plc_read_output(&plc, 63u));

    /* The complete machine-specific P6 command area must decode without
       entering any of the manual-reserved C..E opcode groups. */
    {
        const tnc155_rom_view *p6 = tnc155_rom_get(TNC155_ROM_P6);
        assert(p6 != NULL && p6->size >= 0x1000u);
        tnc155_plc_reset(&plc);
        for (unsigned offset = 0; offset < 0x1000u; offset += 2u) {
            uint16_t word = (uint16_t)((uint16_t)p6->bytes[offset] << 8 |
                                       p6->bytes[offset + 1u]);
            assert(tnc155_plc_step_word(&plc, word) == TNC155_PLC_STEP_OK);
        }
        assert(plc.instructions == 2048u);
        assert(plc.reserved_opcodes == 0u);
        assert(!plc.chain_active);
    }
    puts("discrete one-bit PLC core: OK");
    return 0;
}
