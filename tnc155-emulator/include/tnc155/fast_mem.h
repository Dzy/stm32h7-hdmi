#ifndef TNC155_FAST_MEM_H
#define TNC155_FAST_MEM_H

#include <stdint.h>

#if defined(TNC155_FIRMWARE) && (defined(__GNUC__) || defined(__clang__))
typedef uint16_t tnc155_alias_u16 __attribute__((__may_alias__));
typedef uint32_t tnc155_alias_u32 __attribute__((__may_alias__));

static inline uint16_t tnc155_load_be16_aligned(const uint8_t *p)
{
    return (uint16_t)__builtin_bswap16(*(const tnc155_alias_u16 *)p);
}

static inline void tnc155_store_be16_aligned(uint8_t *p, uint16_t value)
{
    *(tnc155_alias_u16 *)p = (uint16_t)__builtin_bswap16(value);
}

static inline uint32_t tnc155_load_be32_aligned(const uint8_t *p)
{
    return (uint32_t)__builtin_bswap32(*(const tnc155_alias_u32 *)p);
}

static inline void tnc155_store_be32_aligned(uint8_t *p, uint32_t value)
{
    *(tnc155_alias_u32 *)p = (uint32_t)__builtin_bswap32(value);
}
#else
static inline uint16_t tnc155_load_be16_aligned(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static inline void tnc155_store_be16_aligned(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static inline uint32_t tnc155_load_be32_aligned(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
 ((uint32_t)p[1] << 16) |
 ((uint32_t)p[2] << 8) |
 (uint32_t)p[3];
}

static inline void tnc155_store_be32_aligned(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}
#endif

#endif
