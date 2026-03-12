#include <assert.h>
#include <stdint.h>

#include "cpu.h"
#include "codegen_test_support.h"

int
main(void)
{
    const uint32_t rcl8_zero_cf = (1u << 16);
    const uint32_t rcl8_keep_cf = (0u << 16) | C_FLAG;
    const uint32_t rcl16_twice = (2u << 16);
    const uint32_t rcl32_once_cf = (1u << 16) | C_FLAG;
    const uint32_t rcr8_once_cf = (1u << 16) | C_FLAG;
    const uint32_t rcr16_twice = (2u << 16);
    const uint32_t rcr32_once = (1u << 16);
    const uint32_t rcl_compare = new_dynarec_pack_d0d3_rotate_compare(0xd0u, 0x10u, 8u, 0u, 1u, C_FLAG);
    const uint32_t rcr_compare = new_dynarec_pack_d0d3_rotate_compare(0xd3u, 0x18u, 32u, 1u, 4u, 0u);

    assert(new_dynarec_bswap32_result(0x11223344u) == 0x44332211u);
    assert(new_dynarec_bswap32_result(0xaabbccddU) == 0xddccbbaau);

    assert(new_dynarec_aad_result(0x0205u, 10u, 1u) == 0x0019u);
    assert(new_dynarec_aad_result(0x7f80u, 16u, 1u) == 0x0070u);
    assert(new_dynarec_aad_result(0x0205u, 7u, 0u) == 0x0019u);

    assert(new_dynarec_aam_result(0x0029u, 10u, 1u) == 0x0401u);
    assert(new_dynarec_aam_result(0x00ffu, 16u, 1u) == 0x0f0fu);
    assert(new_dynarec_aam_result(0x0029u, 0u, 1u) == 0x0401u);
    assert(new_dynarec_aam_result(0x0029u, 7u, 0u) == 0x0401u);

    assert(new_dynarec_imul_rm16_result(0x0003u, 0x0004u) == 0x000cu);
    assert(new_dynarec_imul_rm16_overflow_flag_mask(0x0003u, 0x0004u) == 0);
    assert(new_dynarec_imul_rm16_result(0x0003u, 0xfffcu) == 0xfff4u);
    assert(new_dynarec_imul_rm16_overflow_flag_mask(0x0003u, 0xfffcu) == 0);
    assert(new_dynarec_imul_rm16_result(0xfffdu, 0xfffcu) == 0x000cu);
    assert(new_dynarec_imul_rm16_overflow_flag_mask(0xfffdu, 0xfffcu) == 0);
    assert(new_dynarec_imul_rm16_result(0x4000u, 0x0002u) == 0x8000u);
    assert(new_dynarec_imul_rm16_overflow_flag_mask(0x4000u, 0x0002u) == (C_FLAG | V_FLAG));
    assert(new_dynarec_imul_rm16_result(0x7fffu, 0x0002u) == 0xfffeu);
    assert(new_dynarec_imul_rm16_overflow_flag_mask(0x7fffu, 0x0002u) == (C_FLAG | V_FLAG));
    assert(new_dynarec_imul_rm16_result(0x8000u, 0x0001u) == 0x8000u);
    assert(new_dynarec_imul_rm16_overflow_flag_mask(0x8000u, 0x0001u) == 0);
    assert(new_dynarec_imul_rm16_result(0xff80u, 0x0002u) == 0xff00u);
    assert(new_dynarec_imul_rm16_overflow_flag_mask(0xff80u, 0x0002u) == 0);

    assert(new_dynarec_imul_rm32_result(0x00000003u, 0x00000004u) == 0x0000000cu);
    assert(new_dynarec_imul_rm32_overflow_flag_mask(0x00000003u, 0x00000004u) == 0);
    assert(new_dynarec_imul_rm32_result(0x00000003u, 0xfffffffcu) == 0xfffffff4u);
    assert(new_dynarec_imul_rm32_overflow_flag_mask(0x00000003u, 0xfffffffcu) == 0);
    assert(new_dynarec_imul_rm32_result(0xfffffffdu, 0xfffffffcu) == 0x0000000cu);
    assert(new_dynarec_imul_rm32_overflow_flag_mask(0xfffffffdu, 0xfffffffcu) == 0);
    assert(new_dynarec_imul_rm32_result(0x40000000u, 0x00000002u) == 0x80000000u);
    assert(new_dynarec_imul_rm32_overflow_flag_mask(0x40000000u, 0x00000002u) == (C_FLAG | V_FLAG));
    assert(new_dynarec_imul_rm32_result(0x7fffffffu, 0x00000002u) == 0xfffffffeu);
    assert(new_dynarec_imul_rm32_overflow_flag_mask(0x7fffffffu, 0x00000002u) == (C_FLAG | V_FLAG));
    assert(new_dynarec_imul_rm32_result(0x80000000u, 0x00000001u) == 0x80000000u);
    assert(new_dynarec_imul_rm32_overflow_flag_mask(0x80000000u, 0x00000001u) == 0);
    assert(new_dynarec_imul_rm32_result(0xffffff80u, 0x00000002u) == 0xffffff00u);
    assert(new_dynarec_imul_rm32_overflow_flag_mask(0xffffff80u, 0x00000002u) == 0);

    assert(new_dynarec_bsf16_result(0x5555u, 0x0000u) == 0x5555u);
    assert(new_dynarec_bsf16_result(0x5555u, 0x8000u) == 15u);
    assert(new_dynarec_bsf16_result(0x5555u, 0x0010u) == 4u);
    assert(new_dynarec_bitscan16_zflag_mask(0x0000u) != 0);
    assert(new_dynarec_bitscan16_zflag_mask(0x0010u) == 0);

    assert(new_dynarec_bsr16_result(0x7777u, 0x0000u) == 0x7777u);
    assert(new_dynarec_bsr16_result(0x7777u, 0x8000u) == 15u);
    assert(new_dynarec_bsr16_result(0x7777u, 0x0010u) == 4u);

    assert(new_dynarec_bsf32_result(0xfeedfaceu, 0x00000000u) == 0xfeedfaceu);
    assert(new_dynarec_bsf32_result(0xfeedfaceu, 0x80000000u) == 31u);
    assert(new_dynarec_bsf32_result(0xfeedfaceu, 0x00000100u) == 8u);
    assert(new_dynarec_bitscan32_zflag_mask(0x00000000u) != 0);
    assert(new_dynarec_bitscan32_zflag_mask(0x00000100u) == 0);

    assert(new_dynarec_bsr32_result(0x0badc0deu, 0x00000000u) == 0x0badc0deu);
    assert(new_dynarec_bsr32_result(0x0badc0deu, 0x80000000u) == 31u);
    assert(new_dynarec_bsr32_result(0x0badc0deu, 0x00000100u) == 8u);

    assert(new_dynarec_rcl8_result(0x80u, rcl8_zero_cf) == 0x00u);
    assert(new_dynarec_rcl8_flag_mask(0x80u, rcl8_zero_cf) == (C_FLAG | V_FLAG));
    assert(new_dynarec_rcl8_result(0x55u, rcl8_keep_cf) == 0x55u);
    assert(new_dynarec_rcl8_flag_mask(0x55u, rcl8_keep_cf) == C_FLAG);

    assert(new_dynarec_rcl16_result(0x8000u, rcl16_twice) == 0x0001u);
    assert(new_dynarec_rcl16_flag_mask(0x8000u, rcl16_twice) == 0u);

    assert(new_dynarec_rcl32_result(0x40000000u, rcl32_once_cf) == 0x80000001u);
    assert(new_dynarec_rcl32_flag_mask(0x40000000u, rcl32_once_cf) == V_FLAG);

    assert(new_dynarec_rcr8_result(0x00u, rcr8_once_cf) == 0x80u);
    assert(new_dynarec_rcr8_flag_mask(0x00u, rcr8_once_cf) == V_FLAG);

    assert(new_dynarec_rcr16_result(0x0001u, rcr16_twice) == 0x8000u);
    assert(new_dynarec_rcr16_flag_mask(0x0001u, rcr16_twice) == V_FLAG);

    assert(new_dynarec_rcr32_result(0x00000003u, rcr32_once) == 0x00000001u);
    assert(new_dynarec_rcr32_flag_mask(0x00000003u, rcr32_once) == C_FLAG);

    assert(new_dynarec_d0d3_rotate_compare_mismatch(0x80u, rcl_compare) == 0);
    assert(new_dynarec_d0d3_rotate_compare_direct_result(0x80u, rcl_compare) == 0x01u);
    assert(new_dynarec_d0d3_rotate_compare_direct_flag_mask(0x80u, rcl_compare) == (C_FLAG | V_FLAG));

    assert(new_dynarec_d0d3_rotate_compare_mismatch(0x80000001u, rcr_compare) == 0);
    assert(new_dynarec_d0d3_rotate_compare_direct_result(0x80000001u, rcr_compare) == 0x28000000u);
    assert(new_dynarec_d0d3_rotate_compare_direct_flag_mask(0x80000001u, rcr_compare) == 0u);

    return 0;
}
