#ifndef _CODEGEN_TEST_SUPPORT_H_
#define _CODEGEN_TEST_SUPPORT_H_

#include <stdint.h>

uint32_t new_dynarec_bswap32_result(uint32_t value);

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

#endif
