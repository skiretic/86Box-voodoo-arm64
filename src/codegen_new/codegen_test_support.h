#ifndef _CODEGEN_TEST_SUPPORT_H_
#define _CODEGEN_TEST_SUPPORT_H_

#include <stdint.h>

#include "codegen_public.h"

uint32_t new_dynarec_bswap32_result(uint32_t value);

uint32_t new_dynarec_aad_result(uint32_t ax, uint32_t base, uint32_t cpu_is_intel);
uint32_t new_dynarec_aam_result(uint32_t ax, uint32_t base, uint32_t cpu_is_intel);

uint32_t new_dynarec_imul_rm16_result(uint32_t dest, uint32_t src);
uint32_t new_dynarec_imul_rm16_overflow_flag_mask(uint32_t dest, uint32_t src);
uint32_t new_dynarec_imul_rm32_result(uint32_t dest, uint32_t src);
uint32_t new_dynarec_imul_rm32_overflow_flag_mask(uint32_t dest, uint32_t src);

uint32_t new_dynarec_bsf16_result(uint32_t dest, uint32_t src);
uint32_t new_dynarec_bsf32_result(uint32_t dest, uint32_t src);
uint32_t new_dynarec_bsr16_result(uint32_t dest, uint32_t src);
uint32_t new_dynarec_bsr32_result(uint32_t dest, uint32_t src);

uint32_t new_dynarec_bitscan16_zflag_mask(uint32_t src);
uint32_t new_dynarec_bitscan32_zflag_mask(uint32_t src);

uint32_t new_dynarec_rcl8_result(uint32_t value, uint32_t packed);
uint32_t new_dynarec_rcl8_flag_mask(uint32_t value, uint32_t packed);
uint32_t new_dynarec_rcl16_result(uint32_t value, uint32_t packed);
uint32_t new_dynarec_rcl16_flag_mask(uint32_t value, uint32_t packed);
uint32_t new_dynarec_rcl32_result(uint32_t value, uint32_t packed);
uint32_t new_dynarec_rcl32_flag_mask(uint32_t value, uint32_t packed);

uint32_t new_dynarec_rcr8_result(uint32_t value, uint32_t packed);
uint32_t new_dynarec_rcr8_flag_mask(uint32_t value, uint32_t packed);
uint32_t new_dynarec_rcr16_result(uint32_t value, uint32_t packed);
uint32_t new_dynarec_rcr16_flag_mask(uint32_t value, uint32_t packed);
uint32_t new_dynarec_rcr32_result(uint32_t value, uint32_t packed);
uint32_t new_dynarec_rcr32_flag_mask(uint32_t value, uint32_t packed);

uint32_t new_dynarec_d0d3_rotate_compare_mismatch(uint32_t value, uint32_t packed_compare);
uint32_t new_dynarec_d0d3_rotate_compare_direct_result(uint32_t value, uint32_t packed_compare);
uint32_t new_dynarec_d0d3_rotate_compare_direct_flag_mask(uint32_t value, uint32_t packed_compare);

#endif
