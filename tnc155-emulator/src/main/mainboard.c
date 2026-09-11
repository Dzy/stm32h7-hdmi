#include "tnc155/mainboard.h"

#include "tnc155/roms.h"
#include "tnc155/fast_mem.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#ifdef TNC155_FIRMWARE
#define TNC155_DIAG_INC(expr_) ((void)0)
#define TNC155_DIAG_ADD(expr_, value_) ((void)0)
#else
#define TNC155_DIAG_INC(expr_) (++(expr_))
#define TNC155_DIAG_ADD(expr_, value_) ((expr_) += (value_))
#endif

static bool main_shared_ram_stall(tnc155_mainboard *board, uint16_t address)
{
    if (board->shared_ram_blocked && address >= 0xf800u) {
        board->shared_ram_stall = true;
        return true;
    }
    return false;
}

static const char *bus_cycle_name(tms9995_bus_cycle cycle)
{
    switch (cycle) {
    case TMS9995_BUS_DATA_READ: return "DATA READ";
    case TMS9995_BUS_OPCODE_FETCH: return "OPCODE FETCH";
    case TMS9995_BUS_DATA_WRITE: return "DATA WRITE";
    case TMS9995_BUS_INTERRUPT_ACK: return "INTERRUPT ACK";
    default: return "UNKNOWN";
    }
}

static void main_bus_fault(tnc155_mainboard *board, bool write,
                           uint16_t logical, uint8_t width, uint16_t value,
                           tms9995_bus_cycle cycle,
                           const tnc155_main_mapper_translation *translation)
{
    tnc155_main_bus_fault *fault;
    unsigned i;

    if (board == NULL || board->bus_fault.active)
        return;

    fault = &board->bus_fault;
    memset(fault, 0, sizeof(*fault));
    fault->active = true;
    fault->write = write;
    fault->cycle = cycle;
    fault->pc = board->cpu.pc;
    fault->wp = board->cpu.wp;
    fault->logical_address = logical;
    fault->width = width;
    fault->value = value;
    if (translation != NULL &&
        translation->route == TNC155_MAIN_MAPPER_ROUTE_EXPANDED) {
        fault->physical_valid = true;
        fault->mapper_segment = translation->segment;
        fault->mapper_code = translation->mapper_code;
        fault->physical_address = translation->expanded_address;
    }

    fprintf(stderr, "\nFATAL MAIN BUS %s\n", write ? "WRITE" : "READ");
    fprintf(stderr, "  PC=>%04X WP=>%04X cycle=%s width=%u\n",
            fault->pc, fault->wp, bus_cycle_name(cycle), (unsigned)width);
    fprintf(stderr, "  logical=>%04X", logical);
    if (fault->physical_valid)
        fprintf(stderr, " segment=%X mapper=>%02X physical=>%05" PRIX32,
                fault->mapper_segment, fault->mapper_code,
                fault->physical_address);
    fputc('\n', stderr);
    if (write)
        fprintf(stderr, "  value=>%04X\n", value);
    fprintf(stderr, "  mapper:");
    for (i = 0u; i < TNC155_MAIN_MAPPER_SEGMENT_COUNT; ++i)
        fprintf(stderr, " %02X", board->mapper.page[i]);
    fprintf(stderr, "\n  No MAIN-board device decodes this address. Emulation halted.\n");
}


typedef enum main_expanded_resource {
    MAIN_EXPANDED_UNKNOWN,
    MAIN_EXPANDED_P3,
    MAIN_EXPANDED_P4,
    MAIN_EXPANDED_P5,
    MAIN_EXPANDED_P6,
    MAIN_EXPANDED_USER_RAM
} main_expanded_resource;

typedef struct main_expanded_decode {
    main_expanded_resource resource;
    uint32_t device_offset;
} main_expanded_decode;

#define MAIN_P3_EXPANDED_BASE   0x00000u
#define MAIN_P4_EXPANDED_BASE   0x10000u
#define MAIN_P5_EXPANDED_BASE   0x20000u
#define MAIN_P6_EXPANDED_BASE   0x30000u
#define MAIN_ROM_EXPANDED_SIZE  0x10000u
#define MAIN_USER_RAM_BASE      0xee000u
#define MAIN_PLC_RAM_BASE       0xff000u

/* Decode the physical 20-bit expanded bus AFTER IC21 has translated the CPU
 * address.  Keeping this outside main_mapper.c is intentional: the mapper has
 * no knowledge of ROM sockets, User RAM, or PLC storage. */
static main_expanded_decode decode_expanded_address(uint32_t physical)
{
    /* Nine 8 KiB devices form one contiguous 72 KiB battery-backed store:
       >EE000..>FFFFF.  Page >FD is ordinary RAM inside this range; it has no
       special read/write plane, checksum hook, or transaction semantics. */
    if (physical >= MAIN_USER_RAM_BASE &&
        physical < MAIN_USER_RAM_BASE + TNC155_MAIN_USER_RAM_SIZE)
        return (main_expanded_decode){MAIN_EXPANDED_USER_RAM,
                                      physical - MAIN_USER_RAM_BASE};

    if (physical < MAIN_P4_EXPANDED_BASE)
        return (main_expanded_decode){MAIN_EXPANDED_P3,
                                      physical - MAIN_P3_EXPANDED_BASE};
    if (physical < MAIN_P5_EXPANDED_BASE)
        return (main_expanded_decode){MAIN_EXPANDED_P4,
                                      physical - MAIN_P4_EXPANDED_BASE};
    if (physical < MAIN_P6_EXPANDED_BASE)
        return (main_expanded_decode){MAIN_EXPANDED_P5,
                                      physical - MAIN_P5_EXPANDED_BASE};
    if (physical < MAIN_P6_EXPANDED_BASE + MAIN_ROM_EXPANDED_SIZE)
        return (main_expanded_decode){MAIN_EXPANDED_P6,
                                      physical - MAIN_P6_EXPANDED_BASE};

    return (main_expanded_decode){MAIN_EXPANDED_UNKNOWN, 0u};
}

static const tnc155_rom_view *rom_for_expanded_resource(
    main_expanded_resource resource)
{
    switch (resource) {
    case MAIN_EXPANDED_P3:
        return tnc155_rom_get(TNC155_ROM_P3);
    case MAIN_EXPANDED_P4:
        return tnc155_rom_get(TNC155_ROM_P4);
    case MAIN_EXPANDED_P5:
        return tnc155_rom_get(TNC155_ROM_P5);
    case MAIN_EXPANDED_P6:
        return tnc155_rom_get(TNC155_ROM_P6);
    default:
        return NULL;
    }
}

static uint8_t read_translated_byte(tnc155_mainboard *board,
                                    uint16_t address,
                                    tms9995_bus_cycle cycle,
                                    bool count_access)
{
    tnc155_main_mapper_translation translation =
        tnc155_main_mapper_translate(&board->mapper, address);
    main_expanded_decode decoded;
    const tnc155_rom_view *rom;

    if (translation.route == TNC155_MAIN_MAPPER_ROUTE_P3_DIRECT) {
        const tnc155_rom_view *p3 = tnc155_rom_get(TNC155_ROM_P3);
        if (p3 != NULL && address < 0x1000u && address < p3->size)
            return p3->bytes[address];
        goto unmapped;
    }

    if (translation.route != TNC155_MAIN_MAPPER_ROUTE_EXPANDED)
        goto unmapped;

    if (count_access)
        TNC155_DIAG_INC(board->mapped_reads[translation.mapper_code]);

    decoded = decode_expanded_address(translation.expanded_address);

    if (decoded.resource == MAIN_EXPANDED_USER_RAM &&
        decoded.device_offset < sizeof(board->user_ram))
        return board->user_ram[decoded.device_offset];

    rom = rom_for_expanded_resource(decoded.resource);
    if (rom != NULL && decoded.device_offset < rom->size)
        return rom->bytes[decoded.device_offset];

unmapped:
    ++board->unmapped_reads;
    board->last_unmapped_address = address;
    main_bus_fault(board, false, address, 1u, 0u, cycle, &translation);
    return 0u;
}

uint8_t tnc155_mainboard_read_byte(tnc155_mainboard *board, uint16_t address,
                                   tms9995_bus_cycle cycle)
{
    (void)cycle;

    if (main_shared_ram_stall(board, address))
        return 0xffu;

    /* P3 accesses the P8279-5 as two byte-wide ports.  The ROM writes command
       >40 to >F789 before reading a key from >F788; reads of >F789 test the
       FIFO count in the status low nibble. */
    if (address == 0xf788u) {
        uint32_t keys_read_before = board->keyboard.keys_read;
        uint8_t value = tnc155_i8279_read_data(&board->keyboard);
        /* F788 is also used for display/sensor RAM.  Start the diagnostic
           trace only when data_r() actually removed a FIFO entry. */
        if (board->keyboard.keys_read != keys_read_before) {
            board->key_trace_key_number = board->keyboard.keys_read;
            board->key_trace_raw_code = value;
            board->key_trace_remaining = value == 0x65u ?
                TNC155_ENTER_TRACE_INSTRUCTIONS :
                TNC155_KEY_TRACE_INSTRUCTIONS;
        }
        return value;
    }
    if (address == 0xf789u)
        return tnc155_i8279_read_status(&board->keyboard);

    if (address == 0xfa16u || address == 0xfa17u) {
        ++board->handshake_reads;
        board->last_handshake_read_pc = board->cpu.pc;
    }
    if (address >= TNC155_MAIN_DIRECT_FIRST) {
        TNC155_DIAG_INC(board->fixed_reads[address - TNC155_MAIN_DIRECT_FIRST]);
        return board->ram[address - TNC155_MAIN_DIRECT_FIRST];
    }

    return read_translated_byte(board, address, cycle, true);
}

void tnc155_mainboard_write_byte(tnc155_mainboard *board, uint16_t address,
                                 uint8_t value)
{
    tnc155_main_mapper_translation translation;

    if (main_shared_ram_stall(board, address))
        return;

    if (address == 0xf788u) {
        tnc155_i8279_write_data(&board->keyboard, value);
        return;
    }
    if (address == 0xf789u) {
        tnc155_i8279_write_command(&board->keyboard, value);
        return;
    }

    translation = tnc155_main_mapper_translate(&board->mapper, address);

    /* P3 >0000..>0FFF is physically on the non-mapped standard-bus path.
       Writes are electrically harmless ROM writes; importantly, mapper
       register 0 cannot redirect this window. */
    if (translation.route == TNC155_MAIN_MAPPER_ROUTE_P3_DIRECT)
        return;

    if (translation.route == TNC155_MAIN_MAPPER_ROUTE_EXPANDED) {
        main_expanded_decode decoded =
            decode_expanded_address(translation.expanded_address);

        TNC155_DIAG_INC(board->mapped_writes[translation.mapper_code]);

#ifndef TNC155_FIRMWARE
        /* Diagnostic only: track writes to the physical final User-RAM page,
           independent of which logical segment currently maps it. */
        if (translation.expanded_address >= MAIN_PLC_RAM_BASE &&
            translation.expanded_address < MAIN_PLC_RAM_BASE + 0x1000u) {
            if (board->mapped_writes[translation.mapper_code] == 1u) {
                board->first_ff_write_pc = board->cpu.pc;
                board->first_ff_write_address = address;
                board->first_ff_write_value = value;
            }
            board->last_ff_write_pc = board->cpu.pc;
            board->last_ff_write_address = address;
            board->last_ff_write_value = value;
        }
#endif

        if (decoded.resource == MAIN_EXPANDED_USER_RAM &&
            decoded.device_offset < sizeof(board->user_ram)) {
            board->user_ram[decoded.device_offset] = value;
            board->user_ram_dirty = true;
            if (decoded.device_offset == 0x0200u &&
                value == (uint8_t)'E' &&
                board->entry_error_first_write_pc == 0u)
                board->entry_error_first_write_pc = board->cpu.pc;
            return;
        }

        /* Writes to a decoded ROM are electrically harmless.  An address
           which does not select any physical device is a reverse-engineering
           error and must stop the machine rather than disappear silently. */
        if (rom_for_expanded_resource(decoded.resource) != NULL)
            return;
        main_bus_fault(board, true, address, 1u, value,
                       TMS9995_BUS_DATA_WRITE, &translation);
        return;
    }

    if (translation.route != TNC155_MAIN_MAPPER_ROUTE_STANDARD_DIRECT) {
        main_bus_fault(board, true, address, 1u, value,
                       TMS9995_BUS_DATA_WRITE, &translation);
        return;
    }

    if (address >= TNC155_MAIN_DIRECT_FIRST) {
        TNC155_DIAG_INC(board->fixed_writes[address - TNC155_MAIN_DIRECT_FIRST]);
        board->ram[address - TNC155_MAIN_DIRECT_FIRST] = value;
        if (address == 0xfa16u || address == 0xfa17u) {
            ++board->handshake_writes;
            board->last_handshake_write_pc = board->cpu.pc;
            board->last_handshake_value =
                (uint16_t)((uint16_t)board->ram[0x1a16u] << 8 |
                           board->ram[0x1a17u]);
        }
    }

    /* Mapper registers are on the standard bus.  The new value takes effect
       immediately for the next CPU access to that segment. */
    if (tnc155_main_mapper_write_register(&board->mapper, address, value))
        TNC155_DIAG_INC(board->mapper_value_writes[value]);
}

static bool plc_program_word(tnc155_mainboard *board, uint16_t address,
                             uint16_t *word)
{
    main_expanded_decode hi_decoded;
    main_expanded_decode lo_decoded;
    const tnc155_rom_view *p6;
    uint32_t hi_expanded;
    uint32_t lo_expanded;
    uint8_t high_byte;
    uint8_t low_byte;

    if (word == NULL || address < 0xa000u || address > 0xaffeu ||
        (address & 1u) != 0u)
        return false;

    /* A complete aligned PLC word is always inside one IC21 4 KiB page.
       Translate the page once; the low byte is physically the next address. */
    hi_expanded = tnc155_main_mapper_expand_mapped(&board->mapper, address,
                                                   NULL);
    lo_expanded = hi_expanded + 1u;
    hi_decoded = decode_expanded_address(hi_expanded);
    lo_decoded = decode_expanded_address(lo_expanded);
    if (hi_decoded.resource != lo_decoded.resource)
        return false;

    if (hi_decoded.resource == MAIN_EXPANDED_P6) {
        if (hi_decoded.device_offset >= 0x1000u ||
            lo_decoded.device_offset >= 0x1000u)
            return false;
        p6 = tnc155_rom_get(TNC155_ROM_P6);
        if (p6 == NULL || lo_decoded.device_offset >= p6->size)
            return false;
        high_byte = p6->bytes[hi_decoded.device_offset];
        low_byte = p6->bytes[lo_decoded.device_offset];
    } else if (hi_decoded.resource == MAIN_EXPANDED_USER_RAM) {
        if (hi_expanded < MAIN_PLC_RAM_BASE ||
            lo_expanded >= MAIN_PLC_RAM_BASE + 0x1000u ||
            lo_decoded.device_offset >= sizeof(board->user_ram))
            return false;
        high_byte = board->user_ram[hi_decoded.device_offset];
        low_byte = board->user_ram[lo_decoded.device_offset];
    } else {
        return false;
    }

    *word = (uint16_t)((uint16_t)high_byte << 8 | low_byte);
    return true;
}

static bool main_direct_word_requires_byte_path(uint16_t address)
{
    /* These byte-wide standard-bus locations have observable per-byte
       behavior.  Word accesses must preserve the original high-byte then
       low-byte handler order.  Odd addresses are already aligned by the
       TMS9995 core, so only the even word starts are listed here. */
    if (address == 0xf788u || address == 0xfa16u)
        return true;
    return address >= TNC155_MAIN_MAPPER_REGISTER_BASE &&
           address <= (uint16_t)(TNC155_MAIN_MAPPER_REGISTER_LAST & 0xfffeu);
}

static uint16_t bus_read_word(void *opaque, uint16_t address,
                              tms9995_bus_cycle cycle)
{
    tnc155_mainboard *board = opaque;
    main_expanded_decode decoded;
    const tnc155_rom_view *rom;
    uint32_t expanded_address;
    uint16_t plc_word;
    uint8_t mapper_code;

    if (main_shared_ram_stall(board, address))
        return 0xffffu;

    if (cycle == TMS9995_BUS_OPCODE_FETCH && address == 0xa000u &&
        plc_program_word(board, address, &plc_word))
        board->plc_aperture_active = true;

    if (cycle == TMS9995_BUS_OPCODE_FETCH && board->plc_aperture_active &&
        address >= 0xa000u && address <= 0xaffeu) {
        if (plc_program_word(board, address, &plc_word)) {
            board->plc.pc = (uint16_t)((address - 0xa000u) >> 1);
            (void)tnc155_plc_step_word(&board->plc, plc_word);
            ++board->plc_steps;
            return 0x1000u;
        }
        board->plc_aperture_active = false;
    }

    if (cycle == TMS9995_BUS_OPCODE_FETCH && board->plc_aperture_active &&
        address == 0xb000u)
        board->plc_aperture_active = false;

    if (address >= TNC155_MAIN_DIRECT_FIRST &&
        !main_direct_word_requires_byte_path(address)) {
        unsigned offset = (unsigned)(address - TNC155_MAIN_DIRECT_FIRST);
        TNC155_DIAG_INC(board->fixed_reads[offset]);
        TNC155_DIAG_INC(board->fixed_reads[offset + 1u]);
        return tnc155_load_be16_aligned(&board->ram[offset]);
    }

    if (address < 0x1000u) {
        const tnc155_rom_view *p3 = tnc155_rom_get(TNC155_ROM_P3);
        if (p3 != NULL && (size_t)address + 1u < p3->size)
            return tnc155_load_be16_aligned(&p3->bytes[address]);
    } else if (address < TNC155_MAIN_DIRECT_FIRST) {
        expanded_address = tnc155_main_mapper_expand_mapped(
            &board->mapper, address, &mapper_code);
        decoded = decode_expanded_address(expanded_address);
        if (decoded.resource == MAIN_EXPANDED_USER_RAM &&
            decoded.device_offset + 1u < sizeof(board->user_ram)) {
            TNC155_DIAG_ADD(board->mapped_reads[mapper_code], 2u);
            return tnc155_load_be16_aligned(
                &board->user_ram[decoded.device_offset]);
        }
        rom = rom_for_expanded_resource(decoded.resource);
        if (rom != NULL && decoded.device_offset + 1u < rom->size) {
            TNC155_DIAG_ADD(board->mapped_reads[mapper_code], 2u);
            return tnc155_load_be16_aligned(&rom->bytes[decoded.device_offset]);
        }
    }

    return (uint16_t)((uint16_t)tnc155_mainboard_read_byte(board, address,
                                                            cycle) << 8 |
                      tnc155_mainboard_read_byte(board,
                                                 (uint16_t)(address + 1u),
                                                 cycle));
}

#ifdef TNC155_FIRMWARE
static uint16_t bus_read_opcode_word(void *opaque, uint16_t address)
{
    tnc155_mainboard *board = opaque;
    main_expanded_decode decoded;
    const tnc155_rom_view *rom;
    uint32_t expanded_address;
    uint16_t plc_word;

    address = (uint16_t)(address & 0xfffeu);

    if (main_shared_ram_stall(board, address))
        return 0xffffu;

    if (address == 0xa000u && plc_program_word(board, address, &plc_word))
        board->plc_aperture_active = true;

    if (board->plc_aperture_active &&
        address >= 0xa000u && address <= 0xaffeu) {
        if (plc_program_word(board, address, &plc_word)) {
            board->plc.pc = (uint16_t)((address - 0xa000u) >> 1);
            (void)tnc155_plc_step_word(&board->plc, plc_word);
            ++board->plc_steps;
            return 0x1000u;
        }
        board->plc_aperture_active = false;
    }

    if (board->plc_aperture_active && address == 0xb000u)
        board->plc_aperture_active = false;

    if (address >= TNC155_MAIN_DIRECT_FIRST &&
        !main_direct_word_requires_byte_path(address)) {
        unsigned offset = (unsigned)(address - TNC155_MAIN_DIRECT_FIRST);
        return tnc155_load_be16_aligned(&board->ram[offset]);
    }

    if (address < 0x1000u) {
        const tnc155_rom_view *p3 = tnc155_rom_get(TNC155_ROM_P3);
        if (p3 != NULL && (size_t)address + 1u < p3->size)
            return tnc155_load_be16_aligned(&p3->bytes[address]);
    } else if (address < TNC155_MAIN_DIRECT_FIRST) {
        expanded_address = tnc155_main_mapper_expand_mapped(
            &board->mapper, address, NULL);
        decoded = decode_expanded_address(expanded_address);
        if (decoded.resource == MAIN_EXPANDED_USER_RAM &&
            decoded.device_offset + 1u < sizeof(board->user_ram))
            return tnc155_load_be16_aligned(
                &board->user_ram[decoded.device_offset]);

        rom = rom_for_expanded_resource(decoded.resource);
        if (rom != NULL && decoded.device_offset + 1u < rom->size)
            return tnc155_load_be16_aligned(&rom->bytes[decoded.device_offset]);
    }

    return bus_read_word(opaque, address, TMS9995_BUS_OPCODE_FETCH);
}

#endif

static uint8_t bus_read_byte(void *opaque, uint16_t address,
                             tms9995_bus_cycle cycle)
{
    return tnc155_mainboard_read_byte(opaque, address, cycle);
}

static void bus_write_word(void *opaque, uint16_t address, uint16_t value,
                           tms9995_bus_cycle cycle)
{
    tnc155_mainboard *board = opaque;
    main_expanded_decode decoded;
    const tnc155_rom_view *rom;
    uint32_t expanded_address;
    uint8_t mapper_code;
    (void)cycle;

    if (main_shared_ram_stall(board, address))
        return;

    if (address >= TNC155_MAIN_DIRECT_FIRST &&
        !main_direct_word_requires_byte_path(address)) {
        unsigned offset = (unsigned)(address - TNC155_MAIN_DIRECT_FIRST);
        TNC155_DIAG_INC(board->fixed_writes[offset]);
        TNC155_DIAG_INC(board->fixed_writes[offset + 1u]);
        tnc155_store_be16_aligned(&board->ram[offset], value);
        return;
    }

    if (address < 0x1000u)
        return;

    if (address < TNC155_MAIN_DIRECT_FIRST) {
        expanded_address = tnc155_main_mapper_expand_mapped(
            &board->mapper, address, &mapper_code);
        decoded = decode_expanded_address(expanded_address);

        if (decoded.resource == MAIN_EXPANDED_USER_RAM &&
            decoded.device_offset + 1u < sizeof(board->user_ram)) {
#ifndef TNC155_FIRMWARE
            uint64_t writes_before = board->mapped_writes[mapper_code];
            board->mapped_writes[mapper_code] = writes_before + 2u;

            if (expanded_address >= MAIN_PLC_RAM_BASE &&
                expanded_address < MAIN_PLC_RAM_BASE + 0x1000u) {
                if (writes_before == 0u) {
                    board->first_ff_write_pc = board->cpu.pc;
                    board->first_ff_write_address = address;
                    board->first_ff_write_value = (uint8_t)(value >> 8);
                }
                board->last_ff_write_pc = board->cpu.pc;
                board->last_ff_write_address = (uint16_t)(address + 1u);
                board->last_ff_write_value = (uint8_t)value;
            }
#endif

            tnc155_store_be16_aligned(&board->user_ram[decoded.device_offset],
                                      value);
            board->user_ram_dirty = true;
            if (decoded.device_offset == 0x0200u &&
                (uint8_t)(value >> 8) == (uint8_t)'E' &&
                board->entry_error_first_write_pc == 0u)
                board->entry_error_first_write_pc = board->cpu.pc;
            return;
        }

        rom = rom_for_expanded_resource(decoded.resource);
        if (rom != NULL) {
            TNC155_DIAG_ADD(board->mapped_writes[mapper_code], 2u);
            return;
        }
    }

    tnc155_mainboard_write_byte(board, address, (uint8_t)(value >> 8));
    tnc155_mainboard_write_byte(board, (uint16_t)(address + 1u),
                                (uint8_t)value);
}

static void bus_write_byte(void *opaque, uint16_t address, uint8_t value,
                           tms9995_bus_cycle cycle)
{
    (void)cycle;
    tnc155_mainboard_write_byte(opaque, address, value);
}

static bool cru_read_bit(void *opaque, uint16_t bit_address)
{
    tnc155_mainboard *board = opaque;
    bool value;

    /* MAIN CRU >0000..>001F is the local V.24/TMS9902 interface.
       In particular, offset >001F is the TMS9902 RESET bit; it is not a
       CLP-processor reset signal. */
    if (bit_address < 0x0020u)
        return tnc155_tms9902_read_cru(&board->serial, bit_address);

    /* Physical E/A belongs to the common machine I/O backplane.  MAIN does
       not decode individual channels here; it only presents the CRU bus. */
    if (tnc155_io_backplane_cru_read_bit(board->io_backplane, bit_address,
                                         &value))
        return value;

    /* R12=>E000..FFFE addresses the discrete PLC's 4K x 1 operand RAM as
       CRU bits >7000..>7FFF.  For example R12=>F9A0 selects >7CD0 = E0. */
    if ((bit_address & 0x7000u) == 0x7000u)
        return tnc155_plc_read_bit(&board->plc, bit_address & 0x0fffu);
    return false;
}

static void cru_write_bit(void *opaque, uint16_t bit_address, bool value)
{
    tnc155_mainboard *board = opaque;
    unsigned cru_slot;

    for (cru_slot = 0; cru_slot < board->cru_write_address_count; ++cru_slot) {
        if (board->cru_write_addresses[cru_slot] == bit_address)
            break;
    }
    if (cru_slot == board->cru_write_address_count &&
        cru_slot < sizeof(board->cru_write_addresses) /
                       sizeof(board->cru_write_addresses[0])) {
        board->cru_write_addresses[cru_slot] = bit_address;
        ++board->cru_write_address_count;
    }
    if (cru_slot < sizeof(board->cru_write_addresses) /
                       sizeof(board->cru_write_addresses[0])) {
        if (board->cru_write_high_counts[cru_slot] == 0u &&
            board->cru_write_low_counts[cru_slot] == 0u)
            board->cru_write_first_pcs[cru_slot] = board->cpu.pc;
        board->cru_write_last_pcs[cru_slot] = board->cpu.pc;
        if (value)
            ++board->cru_write_high_counts[cru_slot];
        else
            ++board->cru_write_low_counts[cru_slot];
    }
    /* MAIN watchdog retrigger.  P3 repeatedly executes
       LI R12,>C8FE; SBZ 0; SBO 0, which places the rising strobe on
       physical CRU bit >647F.  The same bus strobe is also decoded by the
       PLC I/O backplane as A63; CRU devices may legitimately fan out. */
    if (bit_address == 0x647fu && value) {
        board->emergency_monoflop_q = true;
        board->emergency_monoflop_last_trigger_cycle = board->cpu.cycles;
        board->emergency_monoflop_triggered = true;
        ++board->emergency_monoflop_triggers;
    }
    /* MAIN CRU >0000..>001F belongs to its local V.24/TMS9902 device.
       P3 uses R12=>0000; SBO >1F to reset that serial controller.  Never
       couple this CRU bit to the CLP TMS9995 reset input. */
    if (bit_address < 0x0020u) {
        tnc155_tms9902_write_cru(&board->serial, bit_address, value);
        return;
    }

    /* Physical A outputs are selected by CRU write direction on the common
       backplane.  This includes the A31/A63 overload-reset strobes. */
    if (tnc155_io_backplane_cru_write_bit(board->io_backplane, bit_address,
                                          value))
        return;

    if ((bit_address & 0x7000u) == 0x7000u)
        tnc155_plc_write_bit(&board->plc, bit_address & 0x0fffu, value);
}

bool tnc155_mainboard_init(tnc155_mainboard *board)
{
    tms9995_bus bus;
    if (board == NULL)
        return false;
    /* All uninitialized/unused MAIN-board RAM powers up as zero in the
       emulator.  Battery RAM contents may subsequently be replaced by an
       exact persisted image; no page receives an FF or checksum seed. */
    memset(board, 0, sizeof(*board));
    tnc155_main_mapper_reset(&board->mapper);
    tnc155_plc_reset(&board->plc);
    tnc155_i8279_reset(&board->keyboard);
    tnc155_tms9902_reset(&board->serial);
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
    if (!tms9995_reset(&board->cpu))
        return false;

    /* Do not invent a running MAIN monoflop interval at power-on.  The
       service procedure first waits for external control voltage and only
       then exercises the emergency-stop monoflops.  Until firmware emits the
       first observed CRU retrigger pulse, this monoflop is unarmed and must
       not open J1/8 merely because wall-clock time has elapsed. */
    board->emergency_monoflop_triggered = false;
    board->emergency_monoflop_q = false;
    board->emergency_monoflop_last_trigger_cycle = board->cpu.cycles;
    return true;
}


void tnc155_mainboard_attach_io_backplane(tnc155_mainboard *board,
                                          tnc155_io_backplane *backplane)
{
    if (board != NULL)
        board->io_backplane = backplane;
}

bool tnc155_mainboard_load_user_ram(tnc155_mainboard *board,
                                    const char *path)
{
    FILE *file;
    int extra;
    size_t count;

    if (board == NULL || path == NULL)
        return false;
    file = fopen(path, "rb");
    if (file == NULL) {
        memset(board->user_ram, 0, sizeof(board->user_ram));
        board->user_ram_loaded = false;
        board->user_ram_dirty = false;
        return false;
    }
    count = fread(board->user_ram, 1u, sizeof(board->user_ram), file);
    extra = fgetc(file);
    if (fclose(file) != 0 || count != sizeof(board->user_ram) ||
        extra != EOF) {
        memset(board->user_ram, 0, sizeof(board->user_ram));
        board->user_ram_loaded = false;
        board->user_ram_dirty = false;
        return false;
    }
    board->user_ram_loaded = true;
    board->user_ram_dirty = false;
    return true;
}

bool tnc155_mainboard_save_user_ram(tnc155_mainboard *board,
                                    const char *path)
{
    FILE *file;
    size_t count;
    int flush_result;
    int close_result;
    bool ok;

    if (board == NULL || path == NULL)
        return false;
    file = fopen(path, "wb");
    if (file == NULL)
        return false;
    count = fwrite(board->user_ram, 1u, sizeof(board->user_ram), file);
    flush_result = fflush(file);
    close_result = fclose(file);
    ok = count == sizeof(board->user_ram) && flush_result == 0 &&
         close_result == 0;
    if (ok)
        board->user_ram_dirty = false;
    return ok;
}
