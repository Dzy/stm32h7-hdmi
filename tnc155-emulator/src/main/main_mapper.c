#include "tnc155/main_mapper.h"

#include <stddef.h>

void tnc155_main_mapper_reset(tnc155_main_mapper *mapper)
{
    unsigned segment;

    if (mapper == NULL)
        return;
    /* Reset values are identity.  Register 0 is retained because firmware
       writes/tests it, although P3 >0000..>0FFF bypasses IC21 physically.
       Registers 1..D directly control the corresponding expanded-bus pages. */
    for (segment = 0u; segment < TNC155_MAIN_MAPPER_SEGMENT_COUNT; ++segment)
        mapper->page[segment] = (uint8_t)segment;
}

bool tnc155_main_mapper_set_page(tnc155_main_mapper *mapper,
                                  unsigned segment, uint8_t mapper_code)
{
    if (mapper == NULL || segment >= TNC155_MAIN_MAPPER_SEGMENT_COUNT)
        return false;

    mapper->page[segment] = mapper_code;
    return true;
}

bool tnc155_main_mapper_write_register(tnc155_main_mapper *mapper,
                                       uint16_t address, uint8_t value)
{
    unsigned segment;

    if (address < TNC155_MAIN_MAPPER_REGISTER_BASE ||
        address > TNC155_MAIN_MAPPER_REGISTER_LAST)
        return false;

    segment = (unsigned)(address - TNC155_MAIN_MAPPER_REGISTER_BASE);
    return tnc155_main_mapper_set_page(mapper, segment, value);
}

tnc155_main_mapper_translation
tnc155_main_mapper_translate(const tnc155_main_mapper *mapper,
                             uint16_t logical_address)
{
    tnc155_main_mapper_translation result;
    unsigned segment = (unsigned)(logical_address >> 12);

    /* This diagnostic/general API used to memset the whole return struct on
       every access.  Fill only the fields explicitly; STM32 hot paths use the
       inline expand helper and avoid the struct altogether. */
    result.logical_address = logical_address;
    result.segment = (uint8_t)segment;
    result.mapper_code = 0u;
    result.expanded_address = 0u;

    /* P3 contains a documented 4 KiB "not mapped" area.  It stays on the
       standard address bus even while firmware deliberately writes arbitrary
       values to IC21 register 0 during the mapper/RAM self-test. */
    if (logical_address < 0x1000u) {
        result.route = TNC155_MAIN_MAPPER_ROUTE_P3_DIRECT;
        return result;
    }

    /* E/F have no IC21 page registers. */
    if (logical_address >= TNC155_MAIN_DIRECT_FIRST) {
        result.route = TNC155_MAIN_MAPPER_ROUTE_STANDARD_DIRECT;
        return result;
    }

    result.route = TNC155_MAIN_MAPPER_ROUTE_EXPANDED;
    result.expanded_address = tnc155_main_mapper_expand_mapped(
        mapper, logical_address, &result.mapper_code);
    return result;
}

bool tnc155_clp_shared_to_main_q67(uint16_t clp_address,
                                   uint16_t *main_address,
                                   uint16_t *q67_offset)
{
    uint16_t low11;

    if (clp_address < TNC155_CLP_SHARED_BASE)
        return false;

    low11 = clp_address & TNC155_CLP_SHARED_ADDR_MASK;

    if (main_address != NULL)
        *main_address = (uint16_t)(TNC155_CLP_SHARED_BASE | low11);
    if (q67_offset != NULL)
        *q67_offset = (uint16_t)(TNC155_MAIN_Q67_CLP_OFFSET | low11);
    return true;
}
