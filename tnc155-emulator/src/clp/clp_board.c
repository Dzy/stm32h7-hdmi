#include "tnc155/clp_board.h"

#include "tnc155/roms.h"
#include "tnc155/fast_mem.h"

#include <stdio.h>
#include <string.h>

static int gate_trace_enabled;
static uint64_t gate_trace_seq;

void tnc155_clp_gate_trace_enable(int enabled)
{
    gate_trace_enabled = enabled;
    if (!enabled)
        gate_trace_seq = 0;
}

static void gate_trace_access(tnc155_clp_board *board, char rw,
                              uint16_t address, uint8_t value)
{
    if (!gate_trace_enabled)
        return;
    fprintf(stderr,
            "GATE,%llu,%llu,%c,%04X,%02X,%04X,%04X",
            (unsigned long long)++gate_trace_seq,
            (unsigned long long)board->cpu.cycles, rw, address, value,
            board->cpu.pc, board->cpu.wp);
    if (rw == 'W') {
        unsigned r;
        for (r = 0; r < 16; ++r)
            fprintf(stderr, ",R%u=%04X", r,
                    tms9995_get_register(&board->cpu, r));
    }
    fputc('\n', stderr);
}

uint8_t tnc155_clp_font_physical_row(unsigned glyph, unsigned row)
{
    const tnc155_rom_view *font = tnc155_rom_get(TNC155_ROM_P1);
    size_t address;
    if (font == NULL || glyph >= 512u || row >= 32u)
        return 0;
    address = (size_t)glyph * 32u + row;
    return font->bytes[address];
}

uint8_t tnc155_clp_font_row(unsigned mode, unsigned character, unsigned row)
{
    if (mode >= 8u || character >= 64u || row >= 32u)
        return 0;

    /* P1 physical address:
       A13..A11 = mode, A10..A5 = 6-bit character, A4..A0 = row.
       tnc155_clp_font_physical_row() numbers the same 32-byte physical
       slots linearly, so a slot is mode * 64 + character. */
    return tnc155_clp_font_physical_row(mode * 64u + character, row);
}

uint8_t tnc155_clp_read_byte(tnc155_clp_board *board, uint16_t address,
                             tms9995_bus_cycle cycle)
{
    const tnc155_rom_view *p2 = tnc155_rom_get(TNC155_ROM_P2);
    (void)cycle;
    if (address < 0x8000u)
        return p2->bytes[address];
    /* P2 >5B4C probes WORD >8000 against the ROM constant >5555. */
    if (address == 0x8000u || address == 0x8001u)
        return 0x55u;
    if (address >= 0xa000u && address <= 0xbfffu)
        return board->fast_ram[address - 0xa000u];
    if (address >= 0xc000u && address <= 0xdfffu)
        return board->ram[address - 0xc000u];
    if (address == 0xe000u) {
#ifndef TNC155_FIRMWARE
        ++board->gdc_reads;
        board->last_gdc_read_pc = board->cpu.pc;
        if (board->cpu.pc == 0x57eeu) ++board->gdc_poll_reset;
        if (board->cpu.pc == 0x5ab6u) ++board->gdc_poll_empty;
        if (board->cpu.pc == 0x71a0u) ++board->gdc_poll_command;
        if (board->cpu.pc == 0x71b0u) ++board->gdc_poll_parameter;
#endif
        return tnc155_upd7220_status(&board->gdc);
    }
    if (address == 0xe001u) {
#ifndef TNC155_FIRMWARE
        ++board->gdc_reads;
        board->last_gdc_read_pc = board->cpu.pc;
#endif
        return tnc155_upd7220_read_data(&board->gdc);
    }
    if (address >= 0xe800u && address <= 0xe8ffu) {
        uint16_t physical = (uint16_t)(((uint16_t)board->graphics_dram_page << 8) |
                                       (address & 0x00ffu));
#ifndef TNC155_FIRMWARE
        ++board->graphics_dram_reads;
        board->graphics_dram_last_address = physical;
        board->graphics_dram_last_value = board->graphics_dram[physical];
        board->graphics_dram_last_pc = board->cpu.pc;
        board->graphics_dram_last_write = false;
#endif
        return board->graphics_dram[physical];
    }
    if (address >= 0xf100u && address <= 0xf7ffu) {
        uint8_t value = board->gate_array_registers[address - 0xf100u];
#ifndef TNC155_FIRMWARE
        gate_trace_access(board, 'R', address, value);
#endif
        return value;
    }
    if ((address == 0xfa16u || address == 0xfa17u) &&
        board->shared_ram_enabled) {
#ifndef TNC155_FIRMWARE
        ++board->handshake_reads;
        board->last_handshake_read_pc = board->cpu.pc;
#endif
    }
    if (address >= 0xf800u &&
        (size_t)(address - 0xf800u) < board->shared_ram_size) {
        if (board->shared_ram_enabled) {
            board->shared_ram_accessed = true;
            return board->shared_ram[address - 0xf800u];
        }
        #ifndef TNC155_FIRMWARE
        ++board->shared_ram_denied_reads;
#endif
        return 0xffu;
    }
    #ifndef TNC155_FIRMWARE
        ++board->unmapped_reads;
#endif
    board->last_unmapped_address = address;
    return 0xffu;
}

void tnc155_clp_write_byte(tnc155_clp_board *board, uint16_t address,
                           uint8_t value)
{
    if (address >= 0xa000u && address <= 0xbfffu)
        board->fast_ram[address - 0xa000u] = value;
    else if (address >= 0xc000u && address <= 0xdfffu)
        board->ram[address - 0xc000u] = value;
    else if (address == 0xe000u || address == 0xe001u) {
        board->gdc.host_pc = board->cpu.pc;
        if (address == 0xe001u)
            tnc155_upd7220_write_command(&board->gdc, value);
        else
            tnc155_upd7220_write_parameter(&board->gdc, value);
#ifndef TNC155_FIRMWARE
        board->last_gdc_write_pc = board->cpu.pc;
        if (!((board->cpu.pc >= 0x44d0u && board->cpu.pc <= 0x45a8u) ||
              board->cpu.pc == 0x5ac0u) &&
            board->gdc_write_log_count <
            sizeof(board->gdc_write_log) / sizeof(board->gdc_write_log[0])) {
            board->gdc_write_log[board->gdc_write_log_count] =
                (uint16_t)(value | ((address & 1u) << 8));
            board->gdc_write_pc_log[board->gdc_write_log_count++] = board->cpu.pc;
        }
        ++board->gdc_writes;
#endif
    } else if (address >= 0xe800u && address <= 0xe8ffu) {
        uint16_t physical = (uint16_t)(((uint16_t)board->graphics_dram_page << 8) |
                                       (address & 0x00ffu));
        board->graphics_dram[physical] = value;
#ifndef TNC155_FIRMWARE
        ++board->graphics_dram_writes;
        board->graphics_dram_last_address = physical;
        board->graphics_dram_last_value = value;
        board->graphics_dram_last_pc = board->cpu.pc;
        board->graphics_dram_last_write = true;
#endif
    } else if (address >= 0xf100u && address <= 0xf7ffu) {
        /* Keep the physical gate-array register decode, but do not attach any
           virtual mechanics, counters, reference switches or synthetic index
           events to >F400..>F5FF. */
        board->gate_array_registers[address - 0xf100u] = value;
#ifndef TNC155_FIRMWARE
        gate_trace_access(board, 'W', address, value);
#endif
        if (address == 0xf200u || address == 0xf201u) {
            board->graphics_dram_page = value;
#ifndef TNC155_FIRMWARE
            ++board->graphics_dram_page_writes;
            board->graphics_dram_page_last_pc = board->cpu.pc;
#endif
        }
    } else if (address >= 0xf800u &&
               (size_t)(address - 0xf800u) < board->shared_ram_size) {
        if (!board->shared_ram_enabled) {
            #ifndef TNC155_FIRMWARE
        ++board->shared_ram_denied_writes;
#endif
            return;
        }
        board->shared_ram_accessed = true;
        board->shared_ram[address - 0xf800u] = value;
        if (address == 0xfa16u || address == 0xfa17u) {
#ifndef TNC155_FIRMWARE
            ++board->handshake_writes;
            board->last_handshake_write_pc = board->cpu.pc;
            board->last_handshake_value =
                (uint16_t)((uint16_t)board->shared_ram[0x216u] << 8 |
                           board->shared_ram[0x217u]);
#endif
        }
    }
}

static uint16_t bus_read_word(void *opaque, uint16_t address,
                              tms9995_bus_cycle cycle)
{
    tnc155_clp_board *board = opaque;

    /* Hot path: instruction fetches and workspace/data accesses spend almost
       all their time in the three side-effect-free memory regions below.
       Handle a complete 16-bit transfer with one range decode instead of
       calling the byte decoder twice.  Device, gate-array and shared-Q67
       accesses deliberately stay on the byte path so their side effects and
       shared-bus HOLD marker remain exact. */
    if (address <= 0x7ffeu) {
        const tnc155_rom_view *p2 = tnc155_rom_get(TNC155_ROM_P2);
        if (p2 != NULL && (size_t)address + 1u < p2->size)
            return tnc155_load_be16_aligned(&p2->bytes[address]);
    }
    if (address == 0x8000u)
        return 0x5555u;
    if (address >= 0xa000u && address <= 0xbffeu) {
        unsigned offset = (unsigned)(address - 0xa000u);
        return tnc155_load_be16_aligned(&board->fast_ram[offset]);
    }
    if (address >= 0xc000u && address <= 0xdffeu) {
        unsigned offset = (unsigned)(address - 0xc000u);
        return tnc155_load_be16_aligned(&board->ram[offset]);
    }

    return (uint16_t)((uint16_t)tnc155_clp_read_byte(board, address, cycle) << 8 |
                      tnc155_clp_read_byte(board, (uint16_t)(address + 1u), cycle));
}

#ifdef TNC155_FIRMWARE
static uint16_t bus_read_opcode_word(void *opaque, uint16_t address)
{
    tnc155_clp_board *board = opaque;

    address = (uint16_t)(address & 0xfffeu);

    if (address <= 0x7ffeu) {
        const tnc155_rom_view *p2 = tnc155_rom_get(TNC155_ROM_P2);
        if (p2 != NULL && (size_t)address + 1u < p2->size)
            return tnc155_load_be16_aligned(&p2->bytes[address]);
    }
    if (address == 0x8000u)
        return 0x5555u;
    if (address >= 0xa000u && address <= 0xbffeu) {
        unsigned offset = (unsigned)(address - 0xa000u);
        return tnc155_load_be16_aligned(&board->fast_ram[offset]);
    }
    if (address >= 0xc000u && address <= 0xdffeu) {
        unsigned offset = (unsigned)(address - 0xc000u);
        return tnc155_load_be16_aligned(&board->ram[offset]);
    }

    return bus_read_word(opaque, address, TMS9995_BUS_OPCODE_FETCH);
}
#endif

static uint8_t bus_read_byte(void *opaque, uint16_t address,
                             tms9995_bus_cycle cycle)
{
    return tnc155_clp_read_byte(opaque, address, cycle);
}

static void bus_write_word(void *opaque, uint16_t address, uint16_t value,
                           tms9995_bus_cycle cycle)
{
    tnc155_clp_board *board = opaque;
    (void)cycle;

    /* Same fast path for side-effect-free local RAM writes. */
    if (address >= 0xa000u && address <= 0xbffeu) {
        unsigned offset = (unsigned)(address - 0xa000u);
        tnc155_store_be16_aligned(&board->fast_ram[offset], value);
        return;
    }
    if (address >= 0xc000u && address <= 0xdffeu) {
        unsigned offset = (unsigned)(address - 0xc000u);
        tnc155_store_be16_aligned(&board->ram[offset], value);
        return;
    }

    tnc155_clp_write_byte(board, address, (uint8_t)(value >> 8));
    tnc155_clp_write_byte(board, (uint16_t)(address + 1u), (uint8_t)value);
}

static void bus_write_byte(void *opaque, uint16_t address, uint8_t value,
                           tms9995_bus_cycle cycle)
{
    (void)cycle;
    tnc155_clp_write_byte(opaque, address, value);
}

static bool cru_read_bit(void *opaque, uint16_t bit_address)
{
    tnc155_clp_board *board = opaque;
    if (bit_address < 0x0020u)
        return tnc155_tms9902_read_cru(&board->serial, bit_address);
    return false;
}

static void cru_write_bit(void *opaque, uint16_t bit_address, bool value)
{
    tnc155_clp_board *board = opaque;
    if (bit_address >= 0x4040u && bit_address <= 0x404bu) {
        uint16_t mask = (uint16_t)(1u << (bit_address - 0x4040u));
        if (value)
            board->analog.dac_magnitude |= mask;
        else
            board->analog.dac_magnitude &= (uint16_t)~mask;
        board->analog.dac_magnitude &= 0x0fffu;
        #ifndef TNC155_FIRMWARE
        ++board->analog.dac_bit_writes;
#endif
    } else if (bit_address >= 0x404cu && bit_address <= 0x4050u) {
        unsigned channel = bit_address - 0x404cu;
        bool previous = board->analog.sample[channel];
        board->analog.sample[channel] = value;
        /* Observe the physical sample/hold output only.  It does not drive
           any virtual axis model. */
        if (previous && !value) {
            int16_t signed_dac = (int16_t)board->analog.dac_magnitude;
            if (board->analog.dac_negative)
                signed_dac = (int16_t)-signed_dac;
            board->analog.held_dac[channel] = signed_dac;
            #ifndef TNC155_FIRMWARE
        ++board->analog.sample_events[channel];
#endif
        }
    } else if (bit_address == 0x4051u) {
        board->analog.dac_negative = value;
    }
    if (bit_address < 0x0020u)
        tnc155_tms9902_write_cru(&board->serial, bit_address, value);
    if (bit_address == 0x1000u)
        board->shared_ram_enabled = value;
    if (bit_address == 0x4060u && value) {
        board->emergency_monoflop_q = true;
        board->emergency_monoflop_triggered = true;
        board->emergency_monoflop_last_trigger_cycle = board->cpu.cycles;
        ++board->emergency_monoflop_triggers;
    }
#ifndef TNC155_FIRMWARE
    if (board->cru_write_log_count <
        sizeof(board->cru_write_address_log) /
            sizeof(board->cru_write_address_log[0])) {
        uint16_t i = board->cru_write_log_count++;
        board->cru_write_address_log[i] = bit_address;
        board->cru_write_value_log[i] = value ? 1u : 0u;
    }
#endif
}

bool tnc155_clp_board_init(tnc155_clp_board *board)
{
    tms9995_bus bus;
    if (board == NULL)
        return false;
    memset(board, 0, sizeof(*board));

    tnc155_upd7220_init(&board->gdc);
    tnc155_tms9902_reset(&board->serial);
    board->shared_ram = board->shared_ram_storage;
    board->shared_ram_size = sizeof(board->shared_ram_storage);
    memset(&bus, 0, sizeof(bus));
    bus.opaque = board;
    bus.read_word = bus_read_word;
#ifdef TNC155_FIRMWARE
    bus.read_opcode_word = bus_read_opcode_word;
#endif
    bus.read_byte = bus_read_byte;
    bus.write_word = bus_write_word;
    bus.write_byte = bus_write_byte;
    bus.cru_read_bit = cru_read_bit;
    bus.cru_write_bit = cru_write_bit;
    tms9995_init(&board->cpu, &bus);
    return tms9995_reset(&board->cpu);
}

void tnc155_clp_attach_shared_ram(tnc155_clp_board *board, uint8_t *memory,
                                  size_t size)
{
    if (board == NULL || memory == NULL || size < 0x0800u)
        return;
    board->shared_ram = memory;
    board->shared_ram_size = size;
}
