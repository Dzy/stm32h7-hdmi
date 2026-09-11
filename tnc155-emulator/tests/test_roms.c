#include "tnc155/roms.h"
#include "tnc155/panel_keys.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check_sizes(void)
{
    static const size_t expected_sizes[TNC155_ROM_SOCKET_COUNT] = {
        16384u, 32768u, 65536u, 65536u, 65536u, 65536u
    };
    const tnc155_rom_view *rom;
    unsigned i;

    for (i = 0; i < TNC155_ROM_SOCKET_COUNT; ++i) {
        rom = tnc155_rom_get((tnc155_rom_socket)i);
        assert(rom != NULL);
        assert(rom->size == expected_sizes[i]);
        assert(strlen(rom->sha256) == 64u);
        assert(rom->bytes != NULL);
        assert(rom->role != NULL);
    }
}

static int parity_even(unsigned value)
{
    unsigned parity = 0u;
    while (value != 0u) {
        parity ^= value & 1u;
        value >>= 1;
    }
    return parity == 0u;
}

static unsigned p6_crc(const uint8_t *data, size_t size)
{
    unsigned accumulator = 0x2800u;
    size_t i;
    for (i = 0; i < size; ++i) {
        accumulator ^= (unsigned)data[i] << 8;
        if (parity_even(((accumulator >> 8) & 0xffu) & 0xcau))
            accumulator = (accumulator + 0x80u) & 0xffffu;
        accumulator = (accumulator << 1) & 0xffffu;
    }
    return (accumulator >> 8) & 0xffu;
}

static void check_frank_p6(const tnc155_rom_view *rom)
{
    assert(rom != NULL);
    assert(strstr(rom->role, "FRANK PLC 23460102") != NULL);
    assert(rom->bytes[0] == 0xb0 && rom->bytes[1] == 0x00); /* = M0 */
    assert(rom->bytes[2] == 0x28 && rom->bytes[3] == 0x80); /* UN M2176 */
    assert(rom->bytes[1212] == 0x79 && rom->bytes[1213] == 0xb3); /* S M2483 */
    assert(rom->bytes[1214] == 0xff && rom->bytes[0x0fff] == 0xff);
    assert(memcmp(rom->bytes + 0x3900u, "155FRANK ", 9u) == 0);
    assert(p6_crc(rom->bytes, 0x1000u) == rom->bytes[0xfffe]);
    assert(p6_crc(rom->bytes + 0x1000u, 0xeffeu) == rom->bytes[0xffff]);
}

int main(void)
{
    const tnc155_rom_view *rom;

    assert(tnc155_rom_select("tnc155b"));
    assert(tnc155_rom_selected() == TNC155_ROMSET_B);
    assert(strcmp(tnc155_rom_selected_name(), "tnc155b") == 0);
    check_sizes();

    /* TNC155B MAIN reset vector: WP >F000, PC >0A22. */
    rom = tnc155_rom_get(TNC155_ROM_P3);
    assert(rom->bytes[0] == 0xf0 && rom->bytes[1] == 0x00);
    assert(rom->bytes[2] == 0x0a && rom->bytes[3] == 0x22);

    rom = tnc155_rom_get(TNC155_ROM_P1);
    assert(strstr(rom->role, "font") != NULL);

    /* Raw P8279 matrix codes must be translated exactly once by the B P3. */
    rom = tnc155_rom_get(TNC155_ROM_P3);
#define ASSERT_KEY(raw_, logical_) \
    assert((raw_) >= 0x2bu && (raw_) <= 0x7fu && \
           rom->bytes[0x921cu + ((raw_) - 0x2bu)] == (logical_))
    ASSERT_KEY(TNC155_RAW_KEY_P, 0x43u);
    ASSERT_KEY(TNC155_RAW_KEY_I_MODE, 0x44u);
    ASSERT_KEY(TNC155_RAW_KEY_STOP, 0x54u);
    ASSERT_KEY(TNC155_RAW_KEY_ENT, 0x65u);
    ASSERT_KEY(TNC155_RAW_KEY_END, 0x77u);
    ASSERT_KEY(TNC155_RAW_KEY_CE, 0x69u);
    ASSERT_KEY(TNC155_RAW_KEY_X, 0x6du);
    ASSERT_KEY(TNC155_RAW_KEY_Y, 0x6cu);
    ASSERT_KEY(TNC155_RAW_KEY_Z, 0x6bu);
    ASSERT_KEY(TNC155_RAW_KEY_IV, 0x6au);
    ASSERT_KEY(TNC155_RAW_KEY_0, 0x6fu);
    ASSERT_KEY(TNC155_RAW_KEY_1, 0x70u);
    ASSERT_KEY(TNC155_RAW_KEY_2, 0x74u);
    ASSERT_KEY(TNC155_RAW_KEY_3, 0x7du);
    ASSERT_KEY(TNC155_RAW_KEY_4, 0x71u);
    ASSERT_KEY(TNC155_RAW_KEY_5, 0x75u);
    ASSERT_KEY(TNC155_RAW_KEY_6, 0x7eu);
    ASSERT_KEY(TNC155_RAW_KEY_7, 0x72u);
    ASSERT_KEY(TNC155_RAW_KEY_8, 0x76u);
    ASSERT_KEY(TNC155_RAW_KEY_9, 0x7fu);
    ASSERT_KEY(TNC155_RAW_KEY_DECIMAL, 0x73u);
    ASSERT_KEY(TNC155_RAW_KEY_PLUSMINUS, 0x64u);
#undef ASSERT_KEY

    rom = tnc155_rom_get(TNC155_ROM_P5);
    assert(strcmp(rom->sha256,
                  "fed805fdecfeb9ae2f9b9533e0cb5840c1dd2ccbc0c3ebedc0dadd405ef87523") == 0);

    /* B keeps B P6 support firmware, but gets the FRANK standard PLC. */
    rom = tnc155_rom_get(TNC155_ROM_P6);
    assert(rom->bytes[0x1000] == 0x04 && rom->bytes[0x1001] == 0x5b);
    check_frank_p6(rom);

    assert(tnc155_rom_select("tnc155q"));
    assert(tnc155_rom_selected() == TNC155_ROMSET_Q);
    assert(strcmp(tnc155_rom_selected_name(), "tnc155q") == 0);
    check_sizes();

    /* TNC155Q MAIN reset vector: WP >F000, PC >09EC. */
    rom = tnc155_rom_get(TNC155_ROM_P3);
    assert(rom->bytes[0] == 0xf0 && rom->bytes[1] == 0x00);
    assert(rom->bytes[2] == 0x09 && rom->bytes[3] == 0xec);
    assert(strstr(rom->source_filename, "Nr3-2340003M") != NULL);

    /* Q keeps Q P6 support firmware, but gets exactly the same FRANK PLC. */
    rom = tnc155_rom_get(TNC155_ROM_P6);
    assert(strstr(rom->source_filename, "WF5") != NULL);
    assert(strcmp(rom->sha256,
                  "134be062c4573c2da35e5e88fde3f041b28db67d2a8585319c9a2dc31f134514") == 0);
    check_frank_p6(rom);

    assert(!tnc155_rom_select("frankenstein"));
    assert(!tnc155_rom_select("invalid"));
    assert(tnc155_rom_selected() == TNC155_ROMSET_Q);
    assert(tnc155_rom_select("tnc155b"));

    puts("embedded TNC155B/Q ROM sets with FRANK PLC: OK");
    return 0;
}
