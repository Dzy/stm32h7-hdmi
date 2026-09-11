#include "tnc155/mainboard.h"
#include "tnc155/roms.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void run_cru_copy16(tnc155_mainboard *board, uint16_t source_r12,
                           uint16_t destination_r12)
{
    static const uint8_t program[] = {
        0x02, 0x0c, 0x00, 0x00, /* LI   R12,source */
        0x34, 0x01,             /* STCR R1,16      */
        0x02, 0x0c, 0x00, 0x00, /* LI   R12,dest   */
        0x30, 0x01              /* LDCR R1,16      */
    };
    unsigned i;

    for (i = 0u; i < sizeof(program); ++i)
        tnc155_mainboard_write_byte(board, (uint16_t)(0xe180u + i),
                                    program[i]);
    tnc155_mainboard_write_byte(board, 0xe182u,
                                (uint8_t)(source_r12 >> 8));
    tnc155_mainboard_write_byte(board, 0xe183u, (uint8_t)source_r12);
    tnc155_mainboard_write_byte(board, 0xe188u,
                                (uint8_t)(destination_r12 >> 8));
    tnc155_mainboard_write_byte(board, 0xe189u,
                                (uint8_t)destination_r12);

    board->cpu.pc = 0xe180u;
    board->cpu.wp = 0xf1c0u;
    board->cpu.pending_interrupts = 0u;
    board->cpu.interrupt_inhibit = 0u;
    for (i = 0u; i < 4u; ++i)
        assert(tms9995_step(&board->cpu) == TMS9995_STEP_OK);
    assert(board->cpu.pc == 0xe18cu);
}

static void run_mov_word(tnc155_mainboard *board, uint16_t source,
                         uint16_t destination)
{
    const uint16_t pc = 0xe300u;

    /* MOV *R1+,@>destination */
    tnc155_mainboard_write_byte(board, pc, 0xc8u);
    tnc155_mainboard_write_byte(board, (uint16_t)(pc + 1u), 0x31u);
    tnc155_mainboard_write_byte(board, (uint16_t)(pc + 2u),
                                (uint8_t)(destination >> 8));
    tnc155_mainboard_write_byte(board, (uint16_t)(pc + 3u),
                                (uint8_t)destination);
    board->cpu.internal_ram[0x02u] = (uint8_t)(source >> 8);
    board->cpu.internal_ram[0x03u] = (uint8_t)source;
    board->cpu.pc = pc;
    board->cpu.wp = 0xf000u;
    board->cpu.pending_interrupts = 0u;
    board->cpu.interrupt_inhibit = 0u;
    assert(tms9995_step(&board->cpu) == TMS9995_STEP_OK);
    assert(tms9995_get_register(&board->cpu, 1u) ==
           (uint16_t)(source + 2u));
}

int main(void)
{
    tnc155_mainboard board;
    tnc155_io_backplane io_backplane;
    unsigned i;

    tnc155_io_backplane_reset(&io_backplane);
    assert(tnc155_mainboard_init(&board));
    tnc155_mainboard_attach_io_backplane(&board, &io_backplane);
    assert(board.io_backplane == &io_backplane);

    /* Hardware/ROM regression: P3 >0000..>0FFF is the documented 4 KiB
       non-mapped window.  The mapper self-test writes F7C0=01,02,...80 while
       executing at >0Bxx; those writes must not redirect the P3 instruction
       stream. */
    {
        const tnc155_rom_view *p3 = tnc155_rom_get(TNC155_ROM_P3);
        uint8_t expected = p3->bytes[0x0be0u];
        assert(tnc155_mainboard_read_byte(&board, 0x0be0u,
                                          TMS9995_BUS_OPCODE_FETCH) == expected);
        tnc155_mainboard_write_byte(&board, 0xf7c0u, 0x01u);
        assert(board.mapper.page[0] == 0x01u);
        assert(tnc155_mainboard_read_byte(&board, 0x0be0u,
                                          TMS9995_BUS_OPCODE_FETCH) == expected);
        tnc155_mainboard_write_byte(&board, 0xf7c0u, 0x80u);
        assert(board.mapper.page[0] == 0x80u);
        assert(tnc155_mainboard_read_byte(&board, 0x0be0u,
                                          TMS9995_BUS_OPCODE_FETCH) == expected);
        tnc155_mainboard_write_byte(&board, 0xf7c0u, 0x00u);
    }
    assert(board.cpu.wp == 0xf000u);
    assert(board.cpu.pc == 0x0a22u);
    assert(sizeof(board.user_ram) == 9u * 8192u);
    assert(!board.user_ram_loaded);
    assert(!board.user_ram_dirty);
    for (i = 0; i < sizeof(board.ram); ++i)
        assert(board.ram[i] == 0x00u);
    for (i = 0; i < sizeof(board.user_ram); ++i)
        assert(board.user_ram[i] == 0x00u);

    /* Reset/default mapper integration: IC21 is transparent/identity before
       P3 overwrites the page registers.  There is no separate programmed
       state.  The TMS9995 core handles its on-chip addresses before these
       board callbacks are reached. */
    {
        tnc155_mainboard boot;
        unsigned steps = 0u;
        bool changed = false;

        assert(tnc155_mainboard_init(&boot));
        for (i = 0u; i < TNC155_MAIN_MAPPER_SEGMENT_COUNT; ++i)
            assert(boot.mapper.page[i] == (uint8_t)i);
        while (!changed && steps < 4000u) {
            tms9995_step_result result = tms9995_step(&boot.cpu);
            assert(result == TMS9995_STEP_OK || result == TMS9995_STEP_IDLE);
            assert(!boot.bus_fault.active);
            for (i = 0u; i < TNC155_MAIN_MAPPER_SEGMENT_COUNT; ++i)
                if (boot.mapper.page[i] != (uint8_t)i)
                    changed = true;
            ++steps;
        }
        assert(changed);
        {
            static const uint8_t expected[14] = {
                0x00, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x17,
                0x18, 0x19, 0x1a, 0x08, 0x09, 0xfd, 0xee
            };
            while (memcmp(boot.mapper.page, expected, sizeof(expected)) != 0 &&
                   steps < 4200u) {
                tms9995_step_result result = tms9995_step(&boot.cpu);
                assert(result == TMS9995_STEP_OK || result == TMS9995_STEP_IDLE);
                assert(!boot.bus_fault.active);
                ++steps;
            }
            assert(memcmp(boot.mapper.page, expected, sizeof(expected)) == 0);
        }
    }

    /* ROM-proven P8279 ports: status/command at >F789 and data at >F788. */
    assert(tnc155_mainboard_read_byte(&board, 0xf789u,
                                      TMS9995_BUS_DATA_READ) == 0u);
    assert(tnc155_i8279_push_key(&board.keyboard, 0x7fu)); /* CE raw -> >69 */
    assert((tnc155_mainboard_read_byte(&board, 0xf789u,
                                       TMS9995_BUS_DATA_READ) & 7u) == 1u);
    tnc155_mainboard_write_byte(&board, 0xf789u, 0x40u);
    assert(tnc155_mainboard_read_byte(&board, 0xf788u,
                                      TMS9995_BUS_DATA_READ) == 0x7fu);
    assert(board.key_trace_raw_code == 0x7fu);
    assert(board.key_trace_remaining == TNC155_KEY_TRACE_INSTRUCTIONS);
    assert(tnc155_mainboard_read_byte(&board, 0xf789u,
                                      TMS9995_BUS_DATA_READ) == 0u);
    board.key_trace_remaining = 0u;
    tnc155_mainboard_write_byte(&board, 0xf789u, 0x60u);
    (void)tnc155_mainboard_read_byte(&board, 0xf788u,
                                     TMS9995_BUS_DATA_READ);
    assert(board.key_trace_remaining == 0u);

    /* The external MAIN RAM decode covers the complete 8 KiB
       >E000..>FFFF range.  The TMS9995 core overlays its on-chip RAM at its
       documented internal addresses before a bus access reaches this API. */
    for (i = 0; i < 0x2000u; ++i) {
        uint16_t address = (uint16_t)(0xe000u + i);
        if (address != 0xf788u && address != 0xf789u)
            tnc155_mainboard_write_byte(&board, address,
                                        (uint8_t)(i ^ (i >> 8)));
    }
    for (i = 0; i < 0x2000u; ++i) {
        uint16_t address = (uint16_t)(0xe000u + i);
        if (address != 0xf788u && address != 0xf789u)
            assert(tnc155_mainboard_read_byte(&board, address,
                                              TMS9995_BUS_DATA_READ) ==
                   (uint8_t)(i ^ (i >> 8)));
    }

    /* Install the ROM-proven >0E2C mapper image. */
    {
        static const uint8_t image[14] = {
            0x00, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x17,
            0x18, 0x19, 0x1a, 0x08, 0x09, 0xfd, 0xee
        };
        for (i = 0; i < 14; ++i)
            tnc155_mainboard_write_byte(&board, (uint16_t)(0xf7c0u + i),
                                        image[i]);
    }
    assert(tnc155_mainboard_read_byte(&board, 0x1000u,
                                      TMS9995_BUS_DATA_READ) ==
           tnc155_rom_get(TNC155_ROM_P3)->bytes[0xa000]);
    assert(tnc155_mainboard_read_byte(&board, 0xb000u,
                                      TMS9995_BUS_DATA_READ) == 0x0au);
    assert(tnc155_mainboard_read_byte(&board, 0xb001u,
                                      TMS9995_BUS_DATA_READ) == 0x19u);

    /* P3's >0E56 image substitutes writable mapped-RAM page >FF for P6
       page >30.  >EE..>FF span the nine 8 KiB user-RAM devices exactly. */
    tnc155_mainboard_write_byte(&board, 0xf7cau, 0xffu);
    assert(tnc155_mainboard_read_byte(&board, 0xa000u,
                                      TMS9995_BUS_DATA_READ) == 0x00u);
    tnc155_mainboard_write_byte(&board, 0xa000u, 0x5au);
    assert(tnc155_mainboard_read_byte(&board, 0xa000u,
                                      TMS9995_BUS_DATA_READ) == 0x5au);
    assert(tnc155_rom_get(TNC155_ROM_P6)->bytes[0] != 0x5au);
    assert(board.user_ram[0x11000u] == 0x5au);
    board.user_ram[0x11000u] = 0x00u;
    tnc155_mainboard_write_byte(&board, 0xf7cau, 0x08u);

    /* Every mapped RAM page is a distinct slice of the same 72 KiB store. */
    tnc155_mainboard_write_byte(&board, 0xf7c1u, 0xeeu);
    tnc155_mainboard_write_byte(&board, 0x1123u, 0x34u);
    assert(board.user_ram[0x0123u] == 0x34u);
    tnc155_mainboard_write_byte(&board, 0xf7c1u, 0xfeu);
    tnc155_mainboard_write_byte(&board, 0x1123u, 0x56u);
    assert(board.user_ram[0x10123u] == 0x56u);
    tnc155_mainboard_write_byte(&board, 0xf7c1u, 0x0au);

    /* >FD is an ordinary 4 KiB slice of the contiguous 72 KiB User RAM.
       There are no CF5E/CFDE register exceptions, checksum hooks, or
       separate read/write planes. */
    assert(tnc155_mainboard_read_byte(&board, 0xcfdeu,
                                      TMS9995_BUS_DATA_READ) == 0x00u);
    tnc155_mainboard_write_byte(&board, 0xcfdeu, 0x12u);
    assert(tnc155_mainboard_read_byte(&board, 0xcfdeu,
                                      TMS9995_BUS_DATA_READ) == 0x12u);
    assert(board.user_ram[0x0ffdeu] == 0x12u);

    assert(tnc155_mainboard_read_byte(&board, 0xcf5eu,
                                      TMS9995_BUS_DATA_READ) == 0x00u);
    tnc155_mainboard_write_byte(&board, 0xcf5eu, 0x34u);
    assert(tnc155_mainboard_read_byte(&board, 0xcf5eu,
                                      TMS9995_BUS_DATA_READ) == 0x34u);
    assert(board.user_ram[0x0ff5eu] == 0x34u);

    /* Processor ROM in logical segment A remains executable.  The PLC NOP
       generator applies only when TNC selects a PLC program source (>30 or
       >FF), not merely based on the logical Axxx address. */
    assert(tnc155_rom_get(TNC155_ROM_P3)->bytes[0x8876u] == 0x04u);
    assert(tnc155_rom_get(TNC155_ROM_P3)->bytes[0x8877u] == 0xe0u);
    board.cpu.pc = 0xa876u;
    board.cpu.wp = 0xf1c0u;
    board.cpu.pending_interrupts = 0u;
    board.cpu.interrupt_inhibit = 0u;
    assert(tms9995_step(&board.cpu) == TMS9995_STEP_OK);
    assert(board.cpu.pc == 0xa87au);
    assert(board.plc_steps == 0u);

    /* The reset/runtime map leaves A/B on ordinary P3 pages >08/>09.  The
       PLC aperture must NOT open merely because MAIN executes in Axxx. */
    assert(tnc155_mainboard_read_byte(&board, 0xa000u,
                                      TMS9995_BUS_DATA_READ) ==
           tnc155_rom_get(TNC155_ROM_P3)->bytes[0x8000u]);

    /* P3 mapper image >0E48 selects >30/>31 for PLC execution from P6.
       The emulator follows that mapper state; it does not interpret MP77. */
    tnc155_mainboard_write_byte(&board, 0xf7cau, 0x30u);
    tnc155_mainboard_write_byte(&board, 0xf7cbu, 0x31u);
    assert(tnc155_mainboard_read_byte(&board, 0xa000u,
                                      TMS9995_BUS_DATA_READ) ==
           tnc155_rom_get(TNC155_ROM_P6)->bytes[0x0000u]);
    board.cpu.pc = 0xa000u;
    assert(tms9995_step(&board.cpu) == TMS9995_STEP_OK);
    assert(board.cpu.pc == 0xa002u);
    assert(board.plc_steps == 1u);
    assert(board.plc.instructions == 1u);
    assert(board.plc.pc == 0x0000u);
    assert(board.plc.last_word ==
           (uint16_t)((uint16_t)tnc155_rom_get(TNC155_ROM_P6)->bytes[0] << 8 |
                      tnc155_rom_get(TNC155_ROM_P6)->bytes[1]));
    assert(board.plc_aperture_active);

    /* Complete the remaining 2047 NOP fetches.  This proves that the whole
       4 KiB P6 PLC image reaches the discrete PLC through mapper page >30. */
    while (board.cpu.pc != 0xb000u)
        assert(tms9995_step(&board.cpu) == TMS9995_STEP_OK);
    assert(board.plc_steps == 2048u);
    assert(board.plc.instructions == 2048u);
    assert(board.plc.pc == 0x07ffu);
    assert(board.plc.last_word ==
           (uint16_t)((uint16_t)tnc155_rom_get(TNC155_ROM_P6)->bytes[0x0ffeu]
                      << 8 |
                      tnc155_rom_get(TNC155_ROM_P6)->bytes[0x0fffu]));

    /* Segment B is mapper >31 in both PLC source images.  At >B000 normal
       mapper decoding must supply P6 >1000 = B *R11 and close the aperture. */
    tnc155_mainboard_write_byte(&board, 0xf1d6u, 0x12u); /* R11=>1234 */
    tnc155_mainboard_write_byte(&board, 0xf1d7u, 0x34u);
    assert(tms9995_step(&board.cpu) == TMS9995_STEP_OK);
    assert(board.cpu.pc == 0x1234u);
    assert(!board.plc_aperture_active);

    /* P3 mapper image >0E56 substitutes page >FF for >30.  Fill the last
       4 KiB of User RAM with erased/NOP words and a distinctive first word;
       a PLC fetch must now come from RAM, without any MP-specific shortcut. */
    memset(&board.user_ram[0x11000u], 0x00, 0x1000u);
    board.user_ram[0x11000u] = 0x12u;
    board.user_ram[0x11001u] = 0x34u;
    tnc155_plc_reset(&board.plc);
    board.plc_steps = 0u;
    tnc155_mainboard_write_byte(&board, 0xf7cau, 0xffu);
    tnc155_mainboard_write_byte(&board, 0xf7cbu, 0x31u);
    board.cpu.pc = 0xa000u;
    board.cpu.wp = 0xf1c0u;
    board.cpu.pending_interrupts = 0u;
    board.cpu.interrupt_inhibit = 0u;
    assert(tms9995_step(&board.cpu) == TMS9995_STEP_OK);
    assert(board.plc.last_word == 0x1234u);
    assert(board.plc_steps == 1u);
    assert(board.plc.pc == 0x0000u);
    assert(board.plc_aperture_active);
    while (board.cpu.pc != 0xb000u)
        assert(tms9995_step(&board.cpu) == TMS9995_STEP_OK);
    assert(board.plc_steps == 2048u);
    assert(board.plc.pc == 0x07ffu);
    tnc155_mainboard_write_byte(&board, 0xf1d6u, 0x56u); /* R11=>5678 */
    tnc155_mainboard_write_byte(&board, 0xf1d7u, 0x78u);
    assert(tms9995_step(&board.cpu) == TMS9995_STEP_OK);
    assert(board.cpu.pc == 0x5678u);
    assert(!board.plc_aperture_active);

    /* MAIN CRU >7000..>7FFF is the PLC operand-memory window. */
    board.cpu.pc = 0xe000u;
    board.cpu.wp = 0xf1c0u;
    tnc155_mainboard_write_byte(&board, 0xe000u, 0x1du); /* SBO 0 */
    tnc155_mainboard_write_byte(&board, 0xe001u, 0x00u);
    tnc155_mainboard_write_byte(&board, 0xf1d8u, 0xf9u); /* R12=>F9A0 */
    tnc155_mainboard_write_byte(&board, 0xf1d9u, 0xa0u);
    assert(tms9995_step(&board.cpu) == TMS9995_STEP_OK);
    assert(tnc155_plc_read_input(&board.plc, 0u));
    assert(board.plc.last_word == 0x0000u);

    /* P3 >5D56 physical input scan: R12=>C800 means CRUIN >6400. */
    tnc155_io_backplane_set_input_terminal(&io_backplane, 0u, true);
    tnc155_io_backplane_set_input_terminal(&io_backplane, 15u, true);
    tnc155_mainboard_write_byte(&board, 0xe100u, 0x34u); /* STCR R1,16 */
    tnc155_mainboard_write_byte(&board, 0xe101u, 0x01u);
    tnc155_mainboard_write_byte(&board, 0xf1d8u, 0xc8u); /* R12=>C800 */
    tnc155_mainboard_write_byte(&board, 0xf1d9u, 0x00u);
    board.cpu.pc = 0xe100u;
    board.cpu.wp = 0xf1c0u;
    board.cpu.pending_interrupts = 0u;
    board.cpu.interrupt_inhibit = 0u;
    assert(tms9995_step(&board.cpu) == TMS9995_STEP_OK);
    assert(tms9995_get_register(&board.cpu, 1u) == 0x8001u);

    /* P3 output scan: R12=>C880 means CRUOUT >6440, independently of
       reads from that same CRU address range. */
    tnc155_mainboard_write_byte(&board, 0xe102u, 0x30u); /* LDCR R1,16 */
    tnc155_mainboard_write_byte(&board, 0xe103u, 0x01u);
    tnc155_mainboard_write_byte(&board, 0xf1d8u, 0xc8u); /* R12=>C880 */
    tnc155_mainboard_write_byte(&board, 0xf1d9u, 0x80u);
    board.cpu.pc = 0xe102u;
    assert(tms9995_step(&board.cpu) == TMS9995_STEP_OK);
    assert(tnc155_io_backplane_read_output_terminal(&io_backplane, 0u));
    assert(tnc155_io_backplane_read_output_terminal(&io_backplane, 15u));

    /* Complete firmware-mediated physical-I/O bridge.  P3 performs these
       transfers with STCR/LDCR; the emulator must not alias the physical
       backplane and the discrete PLC operand RAM or copy them out of band.

       Inputs:  physical CRU >6400..>647F -> operands E0..E127 (>CD0...).
       Outputs: operands A0..A63 (>E50...) -> physical CRU >6440..>647F. */
    {
        unsigned bit;
        unsigned block;

        tnc155_io_backplane_reset(&io_backplane);
        for (bit = 0u; bit < 128u; ++bit) {
            bool value = (bit % 7u) == 1u || (bit % 11u) == 3u;
            if ((bit & 63u) == 63u)
                tnc155_io_backplane_set_overload(
                    &io_backplane, bit / 64u, !value);
            else
                tnc155_io_backplane_set_input_terminal(&io_backplane, bit,
                                                       value);
        }
        for (block = 0u; block < 8u; ++block)
            run_cru_copy16(&board, (uint16_t)(0xc800u + block * 0x20u),
                           (uint16_t)(0xf9a0u + block * 0x20u));
        for (bit = 0u; bit < 128u; ++bit)
            assert(tnc155_plc_read_input(&board.plc, bit) ==
                   tnc155_io_backplane_read_input_terminal(&io_backplane,
                                                            bit));

        for (bit = 0u; bit < 64u; ++bit)
            tnc155_plc_write_bit(&board.plc,
                                 (uint16_t)(TNC155_PLC_OUTPUT_BASE + bit),
                                 (bit % 5u) == 0u || bit == 31u || bit == 63u);
        for (block = 0u; block < 4u; ++block)
            run_cru_copy16(&board, (uint16_t)(0xfca0u + block * 0x20u),
                           (uint16_t)(0xc880u + block * 0x20u));
        for (bit = 0u; bit < 64u; ++bit) {
            if ((bit & 31u) == 31u)
                assert(!tnc155_io_backplane_read_output_terminal(
                    &io_backplane, bit));
            else
                assert(tnc155_io_backplane_read_output_terminal(
                           &io_backplane, bit) == ((bit % 5u) == 0u));
        }
        assert(io_backplane.card[0].overload_resets == 1u);
        assert(io_backplane.card[1].overload_resets == 1u);
        assert(io_backplane.cru_input_reads == 128u);
        assert(io_backplane.cru_output_writes == 64u);
    }

    /* >31 selects virtual P6 addresses >31000..>31FFF.  The first word is
       the TMS9995 return from the PLC aperture, not a one-bit PLC command. */
    tnc155_mainboard_write_byte(&board, 0xf7cau, 0x31u);
    assert(tnc155_mainboard_read_byte(&board, 0xa000u,
                                      TMS9995_BUS_DATA_READ) ==
           tnc155_rom_get(TNC155_ROM_P6)->bytes[0x1000u]);
    tnc155_mainboard_write_byte(&board, 0xf1d6u, 0x12u); /* R11=>1234 */
    tnc155_mainboard_write_byte(&board, 0xf1d7u, 0x34u);
    board.cpu.pc = 0xa000u;
    assert(tms9995_step(&board.cpu) == TMS9995_STEP_OK);
    assert(board.cpu.pc == 0x1234u);
    assert(board.plc_steps == 2048u);
    assert(board.plc.instructions == 2048u);

    /* >EE is the first 4 KiB page of mapped battery RAM.  It must not alias
       the separately decoded fixed MAIN work RAM at >E000..>FFFF. */
    tnc155_mainboard_write_byte(&board, 0xd234u, 0x5au);
    assert(tnc155_mainboard_read_byte(&board, 0xd234u,
                                      TMS9995_BUS_DATA_READ) == 0x5au);
    assert(board.user_ram[0x0234] == 0x5au);
    assert(tnc155_mainboard_read_byte(&board, 0xe234u,
                                      TMS9995_BUS_DATA_READ) != 0x5au);


    /* Runtime loader map: logical >6B00 is P4 physical >AB00. */
    tnc155_mainboard_write_byte(&board, 0xf7c6u, 0x1au);
    assert(tnc155_mainboard_read_byte(&board, 0x6b00u,
                                      TMS9995_BUS_DATA_READ) == 0x10u);
    assert(tnc155_mainboard_read_byte(&board, 0x6b01u,
                                      TMS9995_BUS_DATA_READ) == 0x80u);

    /* Word fast-path regression: direct RAM, odd-word aliasing, mapped User
       RAM/ROM, Q67, mapper-register writes, FA16/FA17 and P8279 must all
       preserve byte order, counters and device side effects. */
    {
        tnc155_mainboard word_board;
        const tnc155_rom_view *p3 = tnc155_rom_get(TNC155_ROM_P3);
        uint64_t count_before;
        uint32_t handshake_before;
        uint32_t mapper12_before;
        uint32_t mapper34_before;

        assert(tnc155_mainboard_init(&word_board));
        assert(p3 != NULL);
        tnc155_mainboard_write_byte(&word_board, 0xe120u, 0x12u);
        tnc155_mainboard_write_byte(&word_board, 0xe121u, 0x34u);

        run_mov_word(&word_board, 0xe120u, 0xe124u);
        assert(word_board.ram[0x0124u] == 0x12u);
        assert(word_board.ram[0x0125u] == 0x34u);

        /* A0 is ignored for word data cycles, while autoincrement keeps
           the odd effective address odd. */
        run_mov_word(&word_board, 0xe121u, 0xe126u);
        assert(word_board.ram[0x0126u] == 0x12u);
        assert(word_board.ram[0x0127u] == 0x34u);
        assert(tms9995_get_register(&word_board.cpu, 1u) == 0xe123u);

        /* Mapper >EE exposes the first User-RAM page. */
        tnc155_mainboard_write_byte(&word_board, 0xf7c1u, 0xeeu);
        tnc155_mainboard_write_byte(&word_board, 0x1120u, 0xabu);
        tnc155_mainboard_write_byte(&word_board, 0x1121u, 0xcdu);
        count_before = word_board.mapped_reads[0xeeu];
        run_mov_word(&word_board, 0x1120u, 0xe128u);
        assert(word_board.ram[0x0128u] == 0xabu);
        assert(word_board.ram[0x0129u] == 0xcdu);
        assert(word_board.mapped_reads[0xeeu] == count_before + 2u);

        count_before = word_board.mapped_writes[0xeeu];
        run_mov_word(&word_board, 0xe120u, 0x1122u);
        assert(word_board.user_ram[0x0122u] == 0x12u);
        assert(word_board.user_ram[0x0123u] == 0x34u);
        assert(word_board.mapped_writes[0xeeu] == count_before + 2u);

        /* Mapper >0A selects ordinary P3 ROM; reads are big-endian and
           ROM writes remain electrically harmless while counters advance. */
        tnc155_mainboard_write_byte(&word_board, 0xf7c1u, 0x0au);
        count_before = word_board.mapped_reads[0x0au];
        run_mov_word(&word_board, 0x1000u, 0xe12au);
        assert(word_board.ram[0x012au] == p3->bytes[0xa000u]);
        assert(word_board.ram[0x012bu] == p3->bytes[0xa001u]);
        assert(word_board.mapped_reads[0x0au] == count_before + 2u);

        count_before = word_board.mapped_writes[0x0au];
        run_mov_word(&word_board, 0xe120u, 0x1000u);
        assert(word_board.mapped_writes[0x0au] == count_before + 2u);
        assert(p3->bytes[0xa000u] != 0x12u ||
               p3->bytes[0xa001u] != 0x34u);

        /* MAIN's side of Q67 is ordinary fixed RAM. */
        run_mov_word(&word_board, 0xe120u, 0xf800u);
        assert(word_board.ram[0x1800u] == 0x12u);
        assert(word_board.ram[0x1801u] == 0x34u);

        /* Mapper registers must still execute two original byte writes. */
        mapper12_before = word_board.mapper_value_writes[0x12u];
        mapper34_before = word_board.mapper_value_writes[0x34u];
        run_mov_word(&word_board, 0xe120u, 0xf7c0u);
        assert(word_board.mapper.page[0] == 0x12u);
        assert(word_board.mapper.page[1] == 0x34u);
        assert(word_board.mapper_value_writes[0x12u] ==
               mapper12_before + 1u);
        assert(word_board.mapper_value_writes[0x34u] ==
               mapper34_before + 1u);

        /* FA16/FA17 handshake accounting remains byte-exact. */
        handshake_before = word_board.handshake_writes;
        run_mov_word(&word_board, 0xe120u, 0xfa16u);
        assert(word_board.handshake_writes == handshake_before + 2u);
        assert(word_board.last_handshake_value == 0x1234u);

        /* P8279 remains on the slow byte path: the high byte pops one
           FIFO entry and the low byte is the status register. */
        assert(tnc155_i8279_push_key(&word_board.keyboard, 0x5au));
        tnc155_mainboard_write_byte(&word_board, 0xf789u, 0x40u);
        run_mov_word(&word_board, 0xf788u, 0xe12cu);
        assert(word_board.ram[0x012cu] == 0x5au);
        assert(word_board.keyboard.keys_read == 1u);
    }

    /* Battery RAM images are exact raw dumps.  A short/long image must not
       be accepted as valid retained state. */
    {
        const char *path = "test-main-user-ram.bin";
        tnc155_mainboard restored;
        board.user_ram[0] = 0x12u;
        board.user_ram[sizeof(board.user_ram) - 1u] = 0x34u;
        board.user_ram_dirty = true;
        assert(tnc155_mainboard_save_user_ram(&board, path));
        assert(!board.user_ram_dirty);
        assert(tnc155_mainboard_init(&restored));
        assert(tnc155_mainboard_load_user_ram(&restored, path));
        assert(restored.user_ram_loaded);
        assert(restored.user_ram[0] == 0x12u);
        assert(restored.user_ram[sizeof(restored.user_ram) - 1u] == 0x34u);
        assert(memcmp(board.user_ram, restored.user_ram,
                      sizeof(board.user_ram)) == 0);
        assert(remove(path) == 0);
    }
    puts("MAIN reset and mapper: OK");
    return 0;
}
