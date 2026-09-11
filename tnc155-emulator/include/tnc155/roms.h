#ifndef TNC155_ROMS_H
#define TNC155_ROMS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum tnc155_rom_socket {
    TNC155_ROM_P1,
    TNC155_ROM_P2,
    TNC155_ROM_P3,
    TNC155_ROM_P4,
    TNC155_ROM_P5,
    TNC155_ROM_P6,
    TNC155_ROM_SOCKET_COUNT
} tnc155_rom_socket;

typedef enum tnc155_romset {
    TNC155_ROMSET_B,
    TNC155_ROMSET_Q
} tnc155_romset;

typedef struct tnc155_rom_view {
    const char *socket;
    const char *source_filename;
    const char *role;
    const uint8_t *bytes;
    size_t size;
    const char *sha256;
} tnc155_rom_view;

/* Select the base firmware family. Both selections use FRANK PLC 23460102
   in P6 while retaining the selected B or Q P6 support firmware. */
bool tnc155_rom_select(const char *name);
tnc155_romset tnc155_rom_selected(void);
const char *tnc155_rom_selected_name(void);

/* If no programmatic selection has been made, TNC155_ROMSET is read on the
   first ROM access. Example: TNC155_ROMSET=tnc155q ./build/tnc155 */
const tnc155_rom_view *tnc155_rom_get(tnc155_rom_socket socket);

#endif
