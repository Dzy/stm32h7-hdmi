#include "tnc155/serial_keyboard.h"
#include "tnc155/panel_keys.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void feed_text(tnc155_serial_keyboard *serial, tnc155_i8279 *i8279,
                      const char *text)
{
    tnc155_serial_keyboard_feed(serial, i8279, (const uint8_t *)text,
                                strlen(text));
}

int main(void)
{
    tnc155_serial_keyboard serial;
    tnc155_i8279 i8279;
    uint8_t raw = 0u;
    static const uint8_t sequence[] = {
        'K','D','1','0','9',0, 'K','U',0,
        'K','D','1','0','8',0, 'K','U',0,
        'K','D','1','0','7',0, 'K','U',0,
        'K','D','1','0','6',0, 'K','U',0
    };

    tnc155_serial_keyboard_init(&serial);
    tnc155_i8279_reset(&i8279);
    assert(tnc155_serial_keyboard_map_kd(105u, &raw));
    assert(raw == TNC155_RAW_KEY_CE);
    assert(tnc155_serial_keyboard_map_kd(106u, &raw));
    assert(raw == TNC155_RAW_KEY_IV);
    assert(tnc155_serial_keyboard_map_kd(107u, &raw));
    assert(raw == TNC155_RAW_KEY_Z);
    assert(tnc155_serial_keyboard_map_kd(108u, &raw));
    assert(raw == TNC155_RAW_KEY_Y);
    assert(tnc155_serial_keyboard_map_kd(109u, &raw));
    assert(raw == TNC155_RAW_KEY_X);
    assert(!tnc155_serial_keyboard_map_kd(200u, &raw));

    feed_text(&serial, &i8279, "KD105\r\n");
    assert(i8279.fifo_count == 1u);
    assert(tnc155_i8279_read_data(&i8279) == TNC155_RAW_KEY_CE);
    feed_text(&serial, &i8279, "KD105\nKD105\n");
    assert(i8279.fifo_count == 0u);
    assert(serial.duplicate_key_downs == 2u);
    feed_text(&serial, &i8279, "KU\r\nKD105\r\nKU\r\n");
    assert(i8279.fifo_count == 1u);
    assert(tnc155_i8279_read_data(&i8279) == TNC155_RAW_KEY_CE);
    assert(!serial.key_down);

    tnc155_serial_keyboard_feed(&serial, &i8279, sequence,
                                sizeof(sequence));
    assert(i8279.fifo_count == 4u);
    assert(tnc155_i8279_read_data(&i8279) == TNC155_RAW_KEY_X);
    assert(tnc155_i8279_read_data(&i8279) == TNC155_RAW_KEY_Y);
    assert(tnc155_i8279_read_data(&i8279) == TNC155_RAW_KEY_Z);
    assert(tnc155_i8279_read_data(&i8279) == TNC155_RAW_KEY_IV);

    feed_text(&serial, &i8279, "KD999\nKU\n");
    assert(i8279.fifo_count == 0u);
    assert(serial.unmapped_messages == 1u);
    tnc155_serial_keyboard_close(&serial);
    return 0;
}
