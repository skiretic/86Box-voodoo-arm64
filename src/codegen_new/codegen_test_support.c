#include <stdint.h>

#include "cpu.h"

typedef struct {
    uint32_t result;
    uint32_t flag_mask;
} new_dynarec_rotate_result_t;

static new_dynarec_rotate_result_t
new_dynarec_rcl_common(uint32_t value, uint32_t count, uint32_t flags, unsigned width)
{
    const uint32_t mask = (width == 32) ? UINT32_MAX : ((1u << width) - 1u);
    const uint32_t top_bit = 1u << (width - 1);
    uint32_t       carry = (flags & C_FLAG) ? 1u : 0u;
    new_dynarec_rotate_result_t rotate_result;

    value &= mask;
    count &= 0x1fu;
    if (!count) {
        rotate_result.result = value;
        rotate_result.flag_mask = flags & (C_FLAG | V_FLAG);
        return rotate_result;
    }

    while (count-- > 0) {
        const uint32_t next_carry = (value & top_bit) ? 1u : 0u;

        value = ((value << 1) | carry) & mask;
        carry = next_carry;
    }

    rotate_result.result = value;
    rotate_result.flag_mask = carry ? C_FLAG : 0;
    if ((carry ^ ((value >> (width - 1)) & 1u)) != 0)
        rotate_result.flag_mask |= V_FLAG;
    return rotate_result;
}

static inline uint32_t
new_dynarec_rotate_count(uint32_t packed)
{
    return packed >> 16;
}

static inline uint32_t
new_dynarec_rotate_flags(uint32_t packed)
{
    return packed & 0xffffu;
}

static new_dynarec_rotate_result_t
new_dynarec_rcr_common(uint32_t value, uint32_t count, uint32_t flags, unsigned width)
{
    const uint32_t mask = (width == 32) ? UINT32_MAX : ((1u << width) - 1u);
    const uint32_t top_bit = 1u << (width - 1);
    const uint32_t second_top_bit = 1u << (width - 2);
    uint32_t       carry = (flags & C_FLAG) ? 1u : 0u;
    new_dynarec_rotate_result_t rotate_result;

    value &= mask;
    count &= 0x1fu;
    if (!count) {
        rotate_result.result = value;
        rotate_result.flag_mask = flags & (C_FLAG | V_FLAG);
        return rotate_result;
    }

    while (count-- > 0) {
        const uint32_t next_carry = value & 1u;

        value = (value >> 1) | (carry ? top_bit : 0u);
        carry = next_carry;
    }

    rotate_result.result = value & mask;
    rotate_result.flag_mask = carry ? C_FLAG : 0;
    if ((rotate_result.result ^ (rotate_result.result >> 1)) & second_top_bit)
        rotate_result.flag_mask |= V_FLAG;
    return rotate_result;
}

uint32_t
new_dynarec_bswap32_result(uint32_t value)
{
    return __builtin_bswap32(value);
}

uint32_t
new_dynarec_imul_rm16_result(uint32_t dest, uint32_t src)
{
    return (uint16_t) (((int32_t) (int16_t) dest) * ((int32_t) (int16_t) src));
}

uint32_t
new_dynarec_imul_rm16_overflow_flag_mask(uint32_t dest, uint32_t src)
{
    const int32_t result = ((int32_t) (int16_t) dest) * ((int32_t) (int16_t) src);

    return ((result >> 15) != 0 && (result >> 15) != -1) ? (C_FLAG | V_FLAG) : 0;
}

uint32_t
new_dynarec_imul_rm32_result(uint32_t dest, uint32_t src)
{
    return (uint32_t) (((int64_t) (int32_t) dest) * ((int64_t) (int32_t) src));
}

uint32_t
new_dynarec_imul_rm32_overflow_flag_mask(uint32_t dest, uint32_t src)
{
    const int64_t result = ((int64_t) (int32_t) dest) * ((int64_t) (int32_t) src);

    return ((result >> 31) != 0 && (result >> 31) != -1) ? (C_FLAG | V_FLAG) : 0;
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

uint32_t
new_dynarec_rcl8_result(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcl_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 8).result;
}

uint32_t
new_dynarec_rcl8_flag_mask(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcl_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 8).flag_mask;
}

uint32_t
new_dynarec_rcl16_result(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcl_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 16).result;
}

uint32_t
new_dynarec_rcl16_flag_mask(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcl_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 16).flag_mask;
}

uint32_t
new_dynarec_rcl32_result(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcl_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 32).result;
}

uint32_t
new_dynarec_rcl32_flag_mask(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcl_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 32).flag_mask;
}

uint32_t
new_dynarec_rcr8_result(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcr_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 8).result;
}

uint32_t
new_dynarec_rcr8_flag_mask(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcr_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 8).flag_mask;
}

uint32_t
new_dynarec_rcr16_result(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcr_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 16).result;
}

uint32_t
new_dynarec_rcr16_flag_mask(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcr_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 16).flag_mask;
}

uint32_t
new_dynarec_rcr32_result(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcr_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 32).result;
}

uint32_t
new_dynarec_rcr32_flag_mask(uint32_t value, uint32_t packed)
{
    return new_dynarec_rcr_common(value, new_dynarec_rotate_count(packed), new_dynarec_rotate_flags(packed), 32).flag_mask;
}
