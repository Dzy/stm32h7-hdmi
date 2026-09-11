#include "tnc155/main_mapper.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    static const uint8_t reset_runtime[14] = {
        0x00, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x17,
        0x18, 0x19, 0x1a, 0x08, 0x09, 0xfd, 0xee
    };
    tnc155_main_mapper mapper;
    tnc155_main_mapper_translation t;
    uint16_t main_address;
    uint16_t q67_offset;
    unsigned i;

    tnc155_main_mapper_reset(&mapper);

    /* Reset/default state is transparent identity mapping. */
    for (i = 0u; i < TNC155_MAIN_MAPPER_SEGMENT_COUNT; ++i)
        assert(mapper.page[i] == (uint8_t)i);

    t = tnc155_main_mapper_translate(&mapper, 0x0a22u);
    assert(t.route == TNC155_MAIN_MAPPER_ROUTE_P3_DIRECT);

    t = tnc155_main_mapper_translate(&mapper, 0x1234u);
    assert(t.route == TNC155_MAIN_MAPPER_ROUTE_EXPANDED);
    assert(t.mapper_code == 0x01u);
    assert(t.expanded_address == 0x01234u);
    t = tnc155_main_mapper_translate(&mapper, 0xdfffu);
    assert(t.route == TNC155_MAIN_MAPPER_ROUTE_EXPANDED);
    assert(t.mapper_code == 0x0du);
    assert(t.expanded_address == 0x0dfffu);

    /* E/F bypass IC21. */
    t = tnc155_main_mapper_translate(&mapper, 0xf7c0u);
    assert(t.route == TNC155_MAIN_MAPPER_ROUTE_STANDARD_DIRECT);

    /* Every register write takes effect immediately. */
    assert(tnc155_main_mapper_write_register(&mapper, 0xf7c1u, 0x0au));
    t = tnc155_main_mapper_translate(&mapper, 0x1234u);
    assert(t.route == TNC155_MAIN_MAPPER_ROUTE_EXPANDED);
    assert(t.mapper_code == 0x0au);
    assert(t.expanded_address == 0x0a234u);

    assert(tnc155_main_mapper_write_register(&mapper, 0xf7c1u, 0x1au));
    t = tnc155_main_mapper_translate(&mapper, 0x1234u);
    assert(t.mapper_code == 0x1au);
    assert(t.expanded_address == 0x1a234u);

    /* Segment-0 register is writable/testable, but P3's first 4 KiB bypasses
       IC21.  Changing F7C0 must not move the code executing at >0xxx. */
    assert(tnc155_main_mapper_write_register(&mapper, 0xf7c0u, 0xffu));
    assert(mapper.page[0] == 0xffu);
    t = tnc155_main_mapper_translate(&mapper, 0x0123u);
    assert(t.route == TNC155_MAIN_MAPPER_ROUTE_P3_DIRECT);

    /* Check the first ROM-proven reset/runtime image as raw 20-bit mapping. */
    for (i = 0u; i < 14u; ++i)
        assert(tnc155_main_mapper_write_register(
            &mapper, (uint16_t)(0xf7c0u + i), reset_runtime[i]));

    t = tnc155_main_mapper_translate(&mapper, 0x6123u);
    assert(t.mapper_code == 0x17u);
    assert(t.expanded_address == 0x17123u);

    t = tnc155_main_mapper_translate(&mapper, 0xc5e0u);
    assert(t.mapper_code == 0xfdu);
    assert(t.expanded_address == 0xfd5e0u);

    /* Dynamic remap of the same logical C address. */
    assert(tnc155_main_mapper_write_register(&mapper, 0xf7ccu, 0xeeu));
    t = tnc155_main_mapper_translate(&mapper, 0xc5e0u);
    assert(t.mapper_code == 0xeeu);
    assert(t.expanded_address == 0xee5e0u);

    assert(tnc155_clp_shared_to_main_q67(0xf800u,
                                         &main_address, &q67_offset));
    assert(main_address == 0xf800u);
    assert(q67_offset == 0x1800u);

    assert(tnc155_clp_shared_to_main_q67(0xfa16u,
                                         &main_address, &q67_offset));
    assert(main_address == 0xfa16u);
    assert(q67_offset == 0x1a16u);

    assert(tnc155_clp_shared_to_main_q67(0xffffu,
                                         &main_address, &q67_offset));
    assert(main_address == 0xffffu);
    assert(q67_offset == 0x1fffu);

    assert(!tnc155_clp_shared_to_main_q67(0xf7ffu,
                                          &main_address, &q67_offset));

    puts("MAIN mapper translation: OK");
    return 0;
}
