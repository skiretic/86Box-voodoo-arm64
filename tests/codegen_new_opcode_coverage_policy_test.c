#include <assert.h>
#include <stddef.h>

#include "codegen_public.h"

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
    assert(new_dynarec_direct_base_string_opcode_count() == 4);

    for (i = 0; i < sizeof(expected_direct_3dnow_opcodes) / sizeof(expected_direct_3dnow_opcodes[0]); i++)
        assert(new_dynarec_has_direct_3dnow_opcode_recompile(expected_direct_3dnow_opcodes[i]) == 1);

    assert(new_dynarec_has_direct_base_opcode_recompile(0xaa) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xab) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xac) == 1);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xad) == 1);

    assert(new_dynarec_has_direct_base_opcode_recompile(0xa4) == 0);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xa5) == 0);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xa6) == 0);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xa7) == 0);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xae) == 0);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xaf) == 0);

    assert(new_dynarec_has_direct_3dnow_opcode_recompile(0x00) == 0);
    assert(new_dynarec_has_direct_3dnow_opcode_recompile(0x91) == 0);
    assert(new_dynarec_has_direct_3dnow_opcode_recompile(0xbe) == 0);
    assert(new_dynarec_has_direct_base_opcode_recompile(0x9a) == 0);
    assert(new_dynarec_has_direct_base_opcode_recompile(0xc8) == 0);

    return 0;
}
