#include <assert.h>
#include <stddef.h>

#include "codegen_public.h"

extern int new_dynarec_has_direct_0f_opcode_recompile(uint8_t opcode);
extern int new_dynarec_direct_0f_setcc_opcode_count(void);
extern int new_dynarec_direct_0f_bswap_opcode_count(void);
extern int new_dynarec_direct_0f_bitscan_opcode_count(void);

int
main(void)
{
    static const uint8_t expected_direct_3dnow_opcodes[] = {
        0x0c, 0x0d, 0x1c, 0x1d,
        0x8a, 0x8e,
        0x90, 0x94, 0x96, 0x97, 0x9a, 0x9e,
        0xa0, 0xa4, 0xa6, 0xa7, 0xaa, 0xae,
        0xb0, 0xb4, 0xb6, 0xb7, 0xbb, 0xbf,
    };
    size_t i;

    assert(new_dynarec_has_direct_pmaddwd_recompile() == 1);
    assert(new_dynarec_has_direct_3dnow_recompile() == 1);
    assert(new_dynarec_direct_3dnow_opcode_count() == (int) (sizeof(expected_direct_3dnow_opcodes) / sizeof(expected_direct_3dnow_opcodes[0])));
    assert(new_dynarec_direct_base_string_opcode_count() == 6);

    for (i = 0; i < sizeof(expected_direct_3dnow_opcodes) / sizeof(expected_direct_3dnow_opcodes[0]); i++)
        assert(new_dynarec_has_direct_3dnow_opcode_recompile(expected_direct_3dnow_opcodes[i]) == 1);

    assert(new_dynarec_has_direct_base_opcode_recompile(0xaa) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xab) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xac) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xad) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xa5) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0x69) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0x6b) == 1);

    assert(new_dynarec_has_direct_base_opcode_recompile(0xa4) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xa6) == 0);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xa7) == 0);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xae) == 0);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xaf) == 0);
    assert(new_dynarec_direct_0f_setcc_opcode_count() == 16);
    assert(new_dynarec_direct_0f_bswap_opcode_count() == 8);
    assert(new_dynarec_direct_0f_bitscan_opcode_count() == 2);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x90) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x91) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x92) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x93) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x94) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x95) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x96) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x97) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x98) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x99) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x9a) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x9b) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x9c) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x9d) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x9e) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x9f) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xc8) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xc9) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xca) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xcb) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xcc) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xcd) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xce) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xcf) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xbc) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xbd) == 1);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0x8f) == 0);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xa0) == 0);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xc7) == 0);
    assert(new_dynarec_has_direct_0f_opcode_recompile(0xd0) == 0);

    assert(new_dynarec_has_direct_3dnow_opcode_recompile(0x00) == 0);
    assert(new_dynarec_has_direct_3dnow_opcode_recompile(0x91) == 0);
    assert(new_dynarec_has_direct_3dnow_opcode_recompile(0xbe) == 0);
    assert(new_dynarec_has_direct_base_opcode_recompile(0x9a) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xc8) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0x9d) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xca) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xcb) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xf6) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xf7) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xff) == 1);

    return 0;
}
