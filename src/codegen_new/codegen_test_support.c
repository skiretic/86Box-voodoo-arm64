#include <stdint.h>

#include "cpu.h"

uint32_t
new_dynarec_bswap32_result(uint32_t value)
{
    return __builtin_bswap32(value);
}

uint32_t
new_dynarec_bsf16_result(uint32_t dest, uint32_t src)
{
    const uint32_t value = src & 0xffffu;

    if (!value)
        return dest & 0xffffu;

    return (uint32_t) __builtin_ctz(value);
}

uint32_t
new_dynarec_bsf32_result(uint32_t dest, uint32_t src)
{
    if (!src)
        return dest;

    return (uint32_t) __builtin_ctz(src);
}

uint32_t
new_dynarec_bsr16_result(uint32_t dest, uint32_t src)
{
    const uint32_t value = src & 0xffffu;

    if (!value)
        return dest & 0xffffu;

    return 31u - (uint32_t) __builtin_clz(value);
}

uint32_t
new_dynarec_bsr32_result(uint32_t dest, uint32_t src)
{
    if (!src)
        return dest;

    return 31u - (uint32_t) __builtin_clz(src);
}

uint32_t
new_dynarec_bitscan16_zflag_mask(uint32_t src)
{
    return (src & 0xffffu) ? 0 : Z_FLAG;
}

uint32_t
new_dynarec_bitscan32_zflag_mask(uint32_t src)
{
    return src ? 0 : Z_FLAG;
}
