#ifndef TNC155_SERIAL_KEYBOARD_H
#define TNC155_SERIAL_KEYBOARD_H

#include "tnc155/i8279.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TNC155_SERIAL_KEYBOARD_LINE_SIZE 64u
#define TNC155_SERIAL_KEYBOARD_PATH_SIZE 128u

typedef struct tnc155_serial_keyboard {
    int fd;
    char device_path[TNC155_SERIAL_KEYBOARD_PATH_SIZE];
    char line[TNC155_SERIAL_KEYBOARD_LINE_SIZE];
    size_t line_length;
    bool line_overflow;
    bool key_down;
    unsigned active_kd_code;
    uint64_t lines_received;
    uint64_t key_down_messages;
    uint64_t key_up_messages;
    uint64_t unmapped_messages;
    uint64_t duplicate_key_downs;
} tnc155_serial_keyboard;

void tnc155_serial_keyboard_init(tnc155_serial_keyboard *keyboard);
bool tnc155_serial_keyboard_open(tnc155_serial_keyboard *keyboard,
                                 const char *requested_device);
void tnc155_serial_keyboard_close(tnc155_serial_keyboard *keyboard);
void tnc155_serial_keyboard_poll(tnc155_serial_keyboard *keyboard,
                                 tnc155_i8279 *i8279);
void tnc155_serial_keyboard_feed(tnc155_serial_keyboard *keyboard,
                                 tnc155_i8279 *i8279,
                                 const uint8_t *bytes,
                                 size_t byte_count);
bool tnc155_serial_keyboard_map_kd(unsigned kd_code, uint8_t *raw_code);

#endif
