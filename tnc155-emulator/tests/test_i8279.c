#include "tnc155/i8279.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    tnc155_i8279 keyboard;
    unsigned i;

    tnc155_i8279_reset(&keyboard);
    assert(tnc155_i8279_read_status(&keyboard) == 0u);
    tnc155_i8279_write_command(&keyboard, 0x2au);
    assert(keyboard.clock_divider == 10u);

    for (i = 0; i < TNC155_I8279_FIFO_SIZE; ++i)
        assert(tnc155_i8279_push_key(&keyboard, (uint8_t)(0x40u + i)));
    assert(keyboard.irq);
    assert((tnc155_i8279_read_status(&keyboard) & 0x0fu) == 0x08u);
    assert(!tnc155_i8279_push_key(&keyboard, 0x55u));
    assert((tnc155_i8279_read_status(&keyboard) & 0x20u) != 0u);
    for (i = 0; i < TNC155_I8279_FIFO_SIZE; ++i)
        assert(tnc155_i8279_read_data(&keyboard) == (uint8_t)(0x40u + i));
    assert(keyboard.keys_read == TNC155_I8279_FIFO_SIZE);
    assert(keyboard.last_key_accepted == 0x47u);
    assert(keyboard.last_key_read == 0x47u);
    for (i = 0; i < TNC155_I8279_FIFO_SIZE; ++i) {
        assert(keyboard.accepted_history[i] == (uint8_t)(0x40u + i));
        assert(keyboard.read_history[i] == (uint8_t)(0x40u + i));
    }
    assert(!keyboard.irq);
    assert(tnc155_i8279_read_data(&keyboard) == 0xffu);
    assert((tnc155_i8279_read_status(&keyboard) & 0x10u) != 0u);

    tnc155_i8279_write_command(&keyboard, 0xc0u);
    assert(tnc155_i8279_read_status(&keyboard) == 0u);
    tnc155_i8279_write_command(&keyboard, 0x90u);
    tnc155_i8279_write_data(&keyboard, 0x12u);
    tnc155_i8279_write_data(&keyboard, 0x34u);
    tnc155_i8279_write_command(&keyboard, 0x70u);
    assert(tnc155_i8279_read_data(&keyboard) == 0x12u);
    assert(tnc155_i8279_read_data(&keyboard) == 0x34u);

    puts("i8279 keyboard/display controller: OK");
    return 0;
}
