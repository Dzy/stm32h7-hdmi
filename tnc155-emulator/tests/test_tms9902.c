#include "tnc155/tms9902.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    tnc155_tms9902 d = {0};
    uint8_t value = 0;
    unsigned i;
    tnc155_tms9902_reset(&d);
    assert(d.ldctrl && d.ldir && d.lrdr && d.lxdr);
    assert(tnc155_tms9902_read_cru(&d, 22u));
    for (i = 0; i < 8u; ++i)
        tnc155_tms9902_write_cru(&d, i, ((0xa5u >> i) & 1u) != 0u);
    assert(d.control == 0xa5u && !d.ldctrl);
    tnc155_tms9902_write_cru(&d, 13u, false);
    for (i = 0; i < 12u; ++i)
        tnc155_tms9902_write_cru(&d, i, ((0x345u >> i) & 1u) != 0u);
    assert(d.receive_rate == (0x345u & 0x7ffu));
    assert(d.transmit_rate == 0x345u);
    assert(tnc155_tms9902_receive(&d, 0x5au));
    for (i = 0; i < 8u; ++i)
        assert(tnc155_tms9902_read_cru(&d, i) ==
               (((0x5au >> i) & 1u) != 0u));
    tnc155_tms9902_write_cru(&d, 18u, true);
    assert(!d.receive_loaded); /* RIENB write acknowledges RBRL. */
    for (i = 0; i < 8u; ++i)
        tnc155_tms9902_write_cru(&d, i, ((0xc3u >> i) & 1u) != 0u);
    assert(tnc155_tms9902_take_transmit(&d, &value) && value == 0xc3u);
    tnc155_tms9902_write_cru(&d, 31u, true);
    assert(d.reset_count == 2u && d.ldctrl);
    puts("TMS9902 CRU registers and buffers: OK");
    return 0;
}
