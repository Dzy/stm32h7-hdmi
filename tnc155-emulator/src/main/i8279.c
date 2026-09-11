#include "tnc155/i8279.h"

#include <string.h>

enum i8279_io_mode {
    I8279_READ_FIFO = 0,
    I8279_READ_SENSOR,
    I8279_READ_DISPLAY,
    I8279_WRITE_DISPLAY
};

void tnc155_i8279_reset(tnc155_i8279 *device)
{
    if (device == NULL)
        return;
    memset(device, 0, sizeof(*device));
    device->io_mode = I8279_READ_FIFO;
}

uint8_t tnc155_i8279_read_status(const tnc155_i8279 *device)
{
    uint8_t status;
    if (device == NULL)
        return 0xffu;
    status = device->fifo_count & 7u;
    if (device->fifo_count == TNC155_I8279_FIFO_SIZE)
        status |= 0x08u;
    if (device->underrun)
        status |= 0x10u;
    if (device->overrun)
        status |= 0x20u;
    if (device->display_unavailable)
        status |= 0x80u;
    return status;
}

static void increment_address(tnc155_i8279 *device, uint8_t mask)
{
    if (device->auto_increment)
        device->address = (device->address + 1u) & mask;
}

uint8_t tnc155_i8279_read_data(tnc155_i8279 *device)
{
    uint8_t value;
    if (device == NULL)
        return 0xffu;
    if (device->io_mode == I8279_READ_SENSOR) {
        value = device->sensor_ram[device->address & 7u];
        increment_address(device, 7u);
        return value;
    }
    if (device->io_mode == I8279_READ_DISPLAY) {
        value = device->display_ram[device->address & 15u];
        increment_address(device, 15u);
        return value;
    }
    if (device->fifo_count == 0u) {
        device->underrun = true;
        return 0xffu;
    }
    value = device->fifo[device->fifo_head];
    device->fifo_head = (device->fifo_head + 1u) & 7u;
    --device->fifo_count;
    device->irq = device->fifo_count != 0u;
    device->last_key_read = value;
    device->read_history[device->keys_read % TNC155_I8279_KEY_HISTORY_SIZE] =
        value;
    ++device->keys_read;
    return value;
}

void tnc155_i8279_write_command(tnc155_i8279 *device, uint8_t command)
{
    unsigned group;
    if (device == NULL)
        return;
    ++device->commands;
    group = command >> 5;
    switch (group) {
    case 0u: /* Keyboard/display mode set. */
        device->mode = command & 0x1fu;
        break;
    case 1u: /* Program clock. */
        device->clock_divider = command & 0x1fu;
        break;
    case 2u: /* Read FIFO or sensor RAM. */
        device->io_mode = (command & 0x10u) != 0u ?
                          I8279_READ_SENSOR : I8279_READ_FIFO;
        device->auto_increment = (command & 0x10u) != 0u &&
                                 (command & 0x08u) != 0u;
        device->address = command & 7u;
        break;
    case 3u: /* Read display RAM. */
        device->io_mode = I8279_READ_DISPLAY;
        device->auto_increment = (command & 0x10u) != 0u;
        device->address = command & 15u;
        break;
    case 4u: /* Write display RAM. */
        device->io_mode = I8279_WRITE_DISPLAY;
        device->auto_increment = (command & 0x10u) != 0u;
        device->address = command & 15u;
        break;
    case 5u: /* Display write inhibit / blanking. */
        device->write_inhibit_a = (command & 0x08u) != 0u;
        device->write_inhibit_b = (command & 0x04u) != 0u;
        device->blank_a = (command & 0x02u) != 0u;
        device->blank_b = (command & 0x01u) != 0u;
        break;
    case 6u: /* Clear display, FIFO and error flags. */
        memset(device->display_ram, 0, sizeof(device->display_ram));
        device->fifo_head = 0u;
        device->fifo_count = 0u;
        device->irq = false;
        device->overrun = false;
        device->underrun = false;
        break;
    case 7u: /* End interrupt / error-mode set. */
        device->overrun = false;
        device->underrun = false;
        device->irq = device->fifo_count != 0u;
        break;
    }
}

void tnc155_i8279_write_data(tnc155_i8279 *device, uint8_t value)
{
    uint8_t old;
    if (device == NULL || device->io_mode != I8279_WRITE_DISPLAY)
        return;
    old = device->display_ram[device->address & 15u];
    if (!device->write_inhibit_a)
        old = (old & 0xf0u) | (value & 0x0fu);
    if (!device->write_inhibit_b)
        old = (old & 0x0fu) | (value & 0xf0u);
    device->display_ram[device->address & 15u] = old;
    increment_address(device, 15u);
}

bool tnc155_i8279_push_key(tnc155_i8279 *device, uint8_t scan_code)
{
    uint8_t tail;
    if (device == NULL)
        return false;
    if (device->fifo_count == TNC155_I8279_FIFO_SIZE) {
        device->overrun = true;
        ++device->keys_rejected;
        return false;
    }
    tail = (device->fifo_head + device->fifo_count) & 7u;
    device->fifo[tail] = scan_code;
    ++device->fifo_count;
    device->irq = true;
    device->last_key_accepted = scan_code;
    device->accepted_history[
        device->keys_accepted % TNC155_I8279_KEY_HISTORY_SIZE] = scan_code;
    ++device->keys_accepted;
    return true;
}
