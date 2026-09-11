#include "tnc155/clp_board.h"
#include "tnc155/roms.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    tnc155_clp_board board;
    const tnc155_rom_view *font = tnc155_rom_get(TNC155_ROM_P1);
    uint8_t shared[0x0800];

    assert(font != NULL);
    assert(tnc155_clp_board_init(&board));
    assert(board.cpu.wp == 0xf060u);
    assert(board.cpu.pc == 0x25cau);

    /* Local RAM and gate-array decode are distinct address spaces. */
    tnc155_clp_write_byte(&board, 0xb180u, 0x12u);
    assert(tnc155_clp_read_byte(&board, 0xb180u,
                                TMS9995_BUS_DATA_READ) == 0x12u);
    tnc155_clp_write_byte(&board, 0xdc00u, 0x34u);
    assert(tnc155_clp_read_byte(&board, 0xdc00u,
                                TMS9995_BUS_DATA_READ) == 0x34u);
    tnc155_clp_write_byte(&board, 0xd123u, 0x5au);
    tnc155_clp_write_byte(&board, 0xf123u, 0x34u);
    assert(tnc155_clp_read_byte(&board, 0xf123u,
                                TMS9995_BUS_DATA_READ) == 0x34u);
    assert(tnc155_clp_read_byte(&board, 0xd123u,
                                TMS9995_BUS_DATA_READ) == 0x5au);

    /* uPD7220 host ports. */
    board.cpu.pc = 0x1234u;
    tnc155_clp_write_byte(&board, 0xe001u, 0x47u); /* PITCH */
    tnc155_clp_write_byte(&board, 0xe000u, 0x20u);
    assert(board.gdc.fifo_count == 2u);
    tnc155_upd7220_service(&board.gdc, 2u);
    assert(board.gdc.pitch == 0x20u);

    /* P2 board-revision signature. */
    assert(tnc155_clp_read_byte(&board, 0x8000u,
                                TMS9995_BUS_DATA_READ) == 0x55u);
    assert(tnc155_clp_read_byte(&board, 0x8001u,
                                TMS9995_BUS_DATA_READ) == 0x55u);

    /* IC31.1: 64 KiB via the 256-byte banked aperture. */
    tnc155_clp_write_byte(&board, 0xf200u, 0x00u);
    tnc155_clp_write_byte(&board, 0xf201u, 0x3au);
    assert(board.graphics_dram_page == 0x3au);
    tnc155_clp_write_byte(&board, 0xe812u, 0xa5u);
    assert(board.graphics_dram[0x3a12u] == 0xa5u);
    assert(tnc155_clp_read_byte(&board, 0xe812u,
                                TMS9995_BUS_DATA_READ) == 0xa5u);
    tnc155_clp_write_byte(&board, 0xf201u, 0x3bu);
    assert(tnc155_clp_read_byte(&board, 0xe812u,
                                TMS9995_BUS_DATA_READ) == 0x00u);

    /* The former virtual-axis area is now only physical gate-array register
       state.  Reads return exactly what was written; no position, velocity,
       counter, reference-switch or index behavior is synthesized. */
    tnc155_clp_write_byte(&board, 0xf400u, 0x78u);
    tnc155_clp_write_byte(&board, 0xf403u, 0x12u);
    tnc155_clp_write_byte(&board, 0xf408u, 0xa5u);
    tnc155_clp_write_byte(&board, 0xf40au, 0x08u);
    tnc155_clp_write_byte(&board, 0xf40eu, 0x20u);
    assert(tnc155_clp_read_byte(&board, 0xf400u,
                                TMS9995_BUS_DATA_READ) == 0x78u);
    assert(tnc155_clp_read_byte(&board, 0xf403u,
                                TMS9995_BUS_DATA_READ) == 0x12u);
    assert(tnc155_clp_read_byte(&board, 0xf408u,
                                TMS9995_BUS_DATA_READ) == 0xa5u);
    assert(tnc155_clp_read_byte(&board, 0xf40au,
                                TMS9995_BUS_DATA_READ) == 0x08u);
    assert(tnc155_clp_read_byte(&board, 0xf40eu,
                                TMS9995_BUS_DATA_READ) == 0x20u);

    /* Addresses outside the proven IC31.1 aperture remain unmapped. */
    {
        uint32_t unmapped_before = board.unmapped_reads;
        assert(tnc155_clp_read_byte(&board, 0xe900u,
                                    TMS9995_BUS_DATA_READ) == 0xffu);
        assert(board.unmapped_reads == unmapped_before + 1u);
        assert(board.last_unmapped_address == 0xe900u);
    }

    /* CRU >1000 is only a gate for Q67 shared RAM.  Merely enabling it does
       not count as a bus access; the access marker is raised only by an
       actual enabled read/write in >F800..>FFFF. */
    memset(shared, 0, sizeof(shared));
    tnc155_clp_attach_shared_ram(&board, shared, sizeof(shared));
    board.shared_ram_enabled = false;
    board.shared_ram_accessed = false;
    tnc155_clp_write_byte(&board, 0xfa16u, 0x56u);
    assert(!board.shared_ram_accessed);
    assert(tnc155_clp_read_byte(&board, 0xfa16u,
                                TMS9995_BUS_DATA_READ) == 0xffu);
    assert(!board.shared_ram_accessed);

    board.shared_ram_enabled = true;
    assert(!board.shared_ram_accessed);
    tnc155_clp_write_byte(&board, 0xfa16u, 0x56u);
    assert(board.shared_ram_accessed);
    assert(shared[0x216u] == 0x56u);
    board.shared_ram_accessed = false;
    assert(tnc155_clp_read_byte(&board, 0xfa16u,
                                TMS9995_BUS_DATA_READ) == 0x56u);
    assert(board.shared_ram_accessed);

    /* Canonical P1 address examples: mode | char6 | scanline. */
    assert(tnc155_clp_font_row(0u, 1u, 10u) == font->bytes[0x002au]);
    assert(tnc155_clp_font_row(1u, 1u, 10u) == font->bytes[0x082au]);
    assert(tnc155_clp_font_row(2u, 1u, 10u) == font->bytes[0x102au]);
    assert(tnc155_clp_font_row(7u, 63u, 31u) == font->bytes[0x3fffu]);
    assert(tnc155_clp_font_physical_row(511u, 31u) == font->bytes[0x3fffu]);
    assert(tnc155_clp_font_physical_row(512u, 0u) == 0u);

    /* Execute the ROM's real board-revision probe at >5B4C. */
    {
        tnc155_clp_board probe;
        assert(tnc155_clp_board_init(&probe));
        probe.cpu.pc = 0x5b4cu;
        assert(tms9995_step(&probe.cpu) == TMS9995_STEP_OK);
        assert(tms9995_step(&probe.cpu) == TMS9995_STEP_OK);
        assert(tms9995_step(&probe.cpu) == TMS9995_STEP_OK);
        assert(tms9995_step(&probe.cpu) == TMS9995_STEP_OK);
        assert(probe.fast_ram[0x324u] == 0xf2u);
        assert(probe.fast_ram[0x325u] == 0x00u);
    }

    puts("CLP reset, gate-array registers, shared bus and P1 mapping: OK");
    return 0;
}
