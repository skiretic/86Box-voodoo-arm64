#include <assert.h>
#include <stdint.h>

#include "codegen_test_support.h"

int
main(void)
{
    assert(new_dynarec_bswap32_result(0x11223344u) == 0x44332211u);
    assert(new_dynarec_bswap32_result(0xaabbccddU) == 0xddccbbaau);

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

    return 0;
}
