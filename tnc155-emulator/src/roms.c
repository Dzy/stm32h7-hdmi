#include "tnc155/roms.h"

#include "tnc155_rom_images.h"

#ifndef TNC155_FIRMWARE
#include <stdlib.h>
#endif
#include <string.h>

/* STM32 firmware builds select exactly one embedded base firmware family:
 *   -DTNC155_FIRMWARE_ROMSET=0  -> TNC155B P1-P5 + B P6 support + FRANK PLC
 *   -DTNC155_FIRMWARE_ROMSET=1  -> TNC155Q P1-P5 + Q P6 support + FRANK PLC
 * Desktop builds keep the run-time TNC155_ROMSET selector. */
#ifndef TNC155_FIRMWARE_ROMSET
#define TNC155_FIRMWARE_ROMSET 0
#endif

#if defined(TNC155_FIRMWARE)

#if TNC155_FIRMWARE_ROMSET == 0
static tnc155_romset active_romset = TNC155_ROMSET_B;
#define ACTIVE_RECORDS tnc155b_roms
#define ACTIVE_COUNT   tnc155b_rom_count
#elif TNC155_FIRMWARE_ROMSET == 1
static tnc155_romset active_romset = TNC155_ROMSET_Q;
#define ACTIVE_RECORDS tnc155q_roms
#define ACTIVE_COUNT   tnc155q_rom_count
#else
#error "TNC155_FIRMWARE_ROMSET must be 0 (B) or 1 (Q)"
#endif

bool tnc155_rom_select(const char *name)
{
    (void)name;
    return false;
}

tnc155_romset tnc155_rom_selected(void)
{
    return active_romset;
}

const char *tnc155_rom_selected_name(void)
{
#if TNC155_FIRMWARE_ROMSET == 0
    return "tnc155b";
#else
    return "tnc155q";
#endif
}

const tnc155_rom_view *tnc155_rom_get(tnc155_rom_socket socket)
{
    static tnc155_rom_view views[TNC155_ROM_SOCKET_COUNT];
    static int initialized;
    size_t i;

    if (!initialized) {
        for (i = 0; i < ACTIVE_COUNT && i < TNC155_ROM_SOCKET_COUNT; ++i) {
            views[i].socket = ACTIVE_RECORDS[i].socket;
            views[i].source_filename = ACTIVE_RECORDS[i].filename;
            views[i].role = ACTIVE_RECORDS[i].role;
            views[i].bytes = ACTIVE_RECORDS[i].data;
            views[i].size = ACTIVE_RECORDS[i].size;
            views[i].sha256 = ACTIVE_RECORDS[i].sha256;
        }
        initialized = 1;
    }

    if ((unsigned)socket >= TNC155_ROM_SOCKET_COUNT)
        return NULL;
    return &views[socket];
}

#else /* desktop */

static tnc155_romset active_romset = TNC155_ROMSET_B;
static bool selection_initialized;
static bool selection_explicit;

static void ensure_selection(void)
{
    const char *env;

    if (selection_initialized)
        return;
    selection_initialized = true;
    if (selection_explicit)
        return;

    env = getenv("TNC155_ROMSET");
    if (env != NULL && (strcmp(env, "tnc155q") == 0 || strcmp(env, "q") == 0))
        active_romset = TNC155_ROMSET_Q;
    else
        active_romset = TNC155_ROMSET_B;
}

bool tnc155_rom_select(const char *name)
{
    if (name == NULL || strcmp(name, "tnc155b") == 0 || strcmp(name, "b") == 0) {
        active_romset = TNC155_ROMSET_B;
    } else if (strcmp(name, "tnc155q") == 0 || strcmp(name, "q") == 0) {
        active_romset = TNC155_ROMSET_Q;
    } else {
        return false;
    }

    selection_explicit = true;
    selection_initialized = true;
    return true;
}

tnc155_romset tnc155_rom_selected(void)
{
    ensure_selection();
    return active_romset;
}

const char *tnc155_rom_selected_name(void)
{
    return tnc155_rom_selected() == TNC155_ROMSET_Q ? "tnc155q" : "tnc155b";
}

const tnc155_rom_view *tnc155_rom_get(tnc155_rom_socket socket)
{
    static tnc155_rom_view views[2][TNC155_ROM_SOCKET_COUNT];
    static int initialized;
    const tnc155_embedded_rom *records;
    size_t count;
    size_t set_index;
    size_t i;

    ensure_selection();

    if (!initialized) {
        for (set_index = 0; set_index < 2u; ++set_index) {
            if (set_index == 0u) {
                records = tnc155b_roms;
                count = tnc155b_rom_count;
            } else {
                records = tnc155q_roms;
                count = tnc155q_rom_count;
            }

            for (i = 0; i < count && i < TNC155_ROM_SOCKET_COUNT; ++i) {
                views[set_index][i].socket = records[i].socket;
                views[set_index][i].source_filename = records[i].filename;
                views[set_index][i].role = records[i].role;
                views[set_index][i].bytes = records[i].data;
                views[set_index][i].size = records[i].size;
                views[set_index][i].sha256 = records[i].sha256;
            }
        }
        initialized = 1;
    }

    if ((unsigned)socket >= TNC155_ROM_SOCKET_COUNT)
        return NULL;
    return &views[(size_t)active_romset][socket];
}

#endif
