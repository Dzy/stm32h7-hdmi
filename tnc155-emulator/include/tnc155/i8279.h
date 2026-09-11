#ifndef TNC155_I8279_H
#define TNC155_I8279_H

#include <stdbool.h>
#include <stdint.h>

#define TNC155_I8279_FIFO_SIZE 8u
#define TNC155_I8279_DISPLAY_RAM_SIZE 16u
#define TNC155_I8279_SENSOR_RAM_SIZE 8u
#define TNC155_I8279_KEY_HISTORY_SIZE 32u

typedef struct tnc155_i8279 {
    uint8_t mode;
    uint8_t clock_divider;
    uint8_t fifo[TNC155_I8279_FIFO_SIZE];
    uint8_t fifo_head;
    uint8_t fifo_count;
    uint8_t display_ram[TNC155_I8279_DISPLAY_RAM_SIZE];
    uint8_t sensor_ram[TNC155_I8279_SENSOR_RAM_SIZE];
    uint8_t address;
    uint8_t io_mode;
    bool auto_increment;
    bool irq;
    bool overrun;
    bool underrun;
    bool display_unavailable;
    bool write_inhibit_a;
    bool write_inhibit_b;
    bool blank_a;
    bool blank_b;
    uint32_t commands;
    uint32_t keys_accepted;
    uint32_t keys_rejected;
    uint32_t keys_read;
    uint8_t last_key_accepted;
    uint8_t last_key_read;
    uint8_t accepted_history[TNC155_I8279_KEY_HISTORY_SIZE];
    uint8_t read_history[TNC155_I8279_KEY_HISTORY_SIZE];
} tnc155_i8279;

void tnc155_i8279_reset(tnc155_i8279 *device);
uint8_t tnc155_i8279_read_status(const tnc155_i8279 *device);
uint8_t tnc155_i8279_read_data(tnc155_i8279 *device);
void tnc155_i8279_write_command(tnc155_i8279 *device, uint8_t command);
void tnc155_i8279_write_data(tnc155_i8279 *device, uint8_t value);
bool tnc155_i8279_push_key(tnc155_i8279 *device, uint8_t scan_code);

#endif
