#include "tnc155/serial_keyboard.h"

#include <ctype.h>
#include <string.h>

#ifndef TNC155_SERIAL_KEYBOARD_TRACE
#define TNC155_SERIAL_KEYBOARD_TRACE 1
#endif

#if TNC155_SERIAL_KEYBOARD_TRACE
#include <stdio.h>
#define SERIAL_TRACE(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
#define SERIAL_ERROR(...) do { fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define SERIAL_TRACE(...) do { } while (0)
#define SERIAL_ERROR(...) do { } while (0)
#endif

void tnc155_serial_keyboard_init(tnc155_serial_keyboard *keyboard)
{
    if (keyboard == NULL)
        return;
    memset(keyboard, 0, sizeof(*keyboard));
    keyboard->fd = -1;
}

bool tnc155_serial_keyboard_map_kd(unsigned kd_code, uint8_t *raw_code)
{
    static const uint8_t logical_by_raw[] = {
        0x78, 0x79, 0x7a, 0x7b, 0x80, 0x80, 0x80, 0x80,
        0x3b, 0x62, 0x45, 0x46, 0x3c, 0x80, 0x80, 0x80,
        0x47, 0x3d, 0x4d, 0x3e, 0x3f, 0x40, 0x41, 0x42,
        0x43, 0x44, 0x80, 0x80, 0x80, 0x48, 0x49, 0x4a,
        0x4b, 0x4c, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
        0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x4e,
        0x63, 0x64, 0x65, 0x66, 0x67, 0x72, 0x7f, 0x7e,
        0x7d, 0x7c, 0x77, 0x76, 0x75, 0x74, 0x73, 0x68,
        0x71, 0x70, 0x6f, 0x6e, 0x6d, 0x80, 0x80, 0x80,
        0x80, 0x6c, 0x6b, 0x6a, 0x69
    };
    const unsigned raw_first = 0x2bu;
    const unsigned raw_last = 0x7fu;
    unsigned raw;

    if (kd_code > 0xffu ||
        sizeof(logical_by_raw) != raw_last - raw_first + 1u)
        return false;

    for (raw = raw_first; raw <= raw_last; ++raw) {
        uint8_t logical = logical_by_raw[raw - raw_first];
        if (logical != 0x80u && logical == (uint8_t)kd_code) {
            if (raw_code != NULL)
                *raw_code = (uint8_t)raw;
            return true;
        }
    }
    return false;
}

static void process_line(tnc155_serial_keyboard *keyboard,
                         tnc155_i8279 *i8279)
{
    char *start;
    char *end;
    unsigned value = 0u;
    uint8_t raw_code;

    keyboard->line[keyboard->line_length] = '\0';
    start = keyboard->line;
    while (*start != '\0' && isspace((unsigned char)*start))
        ++start;
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1]))
        --end;
    *end = '\0';
    if (*start == '\0')
        return;

    ++keyboard->lines_received;
    SERIAL_TRACE("Serial keyboard RX: %s", start);

    if (strcmp(start, "KU") == 0) {
        ++keyboard->key_up_messages;
        if (keyboard->key_down)
            SERIAL_TRACE(" -> key up (KD%u)\n", keyboard->active_kd_code);
        else
            SERIAL_TRACE(" -> key up\n");
        keyboard->key_down = false;
        keyboard->active_kd_code = 0u;
        return;
    }

    if (start[0] == 'K' && start[1] == 'D' &&
        isdigit((unsigned char)start[2])) {
        const char *p = start + 2;
        while (isdigit((unsigned char)*p)) {
            value = value * 10u + (unsigned)(*p - '0');
            if (value > 65535u)
                break;
            ++p;
        }
        if (*p == '\0' && value <= 65535u) {
            ++keyboard->key_down_messages;
            if (keyboard->key_down && keyboard->active_kd_code == value) {
                ++keyboard->duplicate_key_downs;
                SERIAL_TRACE(" -> duplicate/held\n");
                return;
            }
            keyboard->key_down = true;
            keyboard->active_kd_code = value;
            if (tnc155_serial_keyboard_map_kd(value, &raw_code)) {
                SERIAL_TRACE(" -> P8279 raw >%02X", (unsigned)raw_code);
                if (!tnc155_i8279_push_key(i8279, raw_code)) {
                    SERIAL_TRACE(" (FIFO full, dropped)\n");
                    SERIAL_ERROR("Serial keyboard: P8279 FIFO full, KD%u dropped\n",
                                 value);
                } else {
                    SERIAL_TRACE("\n");
                }
                return;
            }
            ++keyboard->unmapped_messages;
            SERIAL_TRACE(" -> unmapped\n");
            SERIAL_ERROR("Serial keyboard: unmapped KD%u\n", value);
            return;
        }
    }

    ++keyboard->unmapped_messages;
    SERIAL_TRACE(" -> ignored\n");
    SERIAL_ERROR("Serial keyboard: ignored message '%s'\n", start);
}

void tnc155_serial_keyboard_feed(tnc155_serial_keyboard *keyboard,
                                 tnc155_i8279 *i8279,
                                 const uint8_t *bytes,
                                 size_t byte_count)
{
    size_t i;

    if (keyboard == NULL || i8279 == NULL || bytes == NULL)
        return;

    for (i = 0u; i < byte_count; ++i) {
        unsigned char byte = bytes[i];
        if (byte == '\r' || byte == '\n' || byte == '\0') {
            if (keyboard->line_overflow) {
                ++keyboard->unmapped_messages;
                SERIAL_ERROR("Serial keyboard: overlong message discarded\n");
            } else if (keyboard->line_length != 0u) {
                process_line(keyboard, i8279);
            }
            keyboard->line_length = 0u;
            keyboard->line_overflow = false;
            continue;
        }
        if (keyboard->line_overflow)
            continue;
        if (keyboard->line_length + 1u >= sizeof(keyboard->line)) {
            keyboard->line_overflow = true;
            continue;
        }
        keyboard->line[keyboard->line_length++] = (char)byte;
    }
}
