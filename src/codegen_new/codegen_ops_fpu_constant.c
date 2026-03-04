#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_flags.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "x87_sf.h"
#include "x87.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_fpu_constant.h"
#include "codegen_ops_helpers.h"

uint32_t
ropFLD1(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_temp0, 1);
    uop_MOV_DOUBLE_INT(ir, IREG_ST(-1), IREG_temp0);
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
    fpu_PUSH(block, ir);

    return op_pc;
}
uint32_t
ropFLDZ(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_temp0, 0);
    uop_MOV_DOUBLE_INT(ir, IREG_ST(-1), IREG_temp0);
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
    fpu_PUSH(block, ir);

    return op_pc;
}

/*
 * FPU transcendental constant load helpers.
 * These push the constant value onto the FPU stack at runtime.
 * We use CALL_FUNC because the constants are irrational values
 * that cannot be represented as integer MOV_IMM + MOV_DOUBLE_INT.
 */
static void
helper_FLDL2T(void)
{
    cpu_state.TOP--;
    cpu_state.ST[cpu_state.TOP & 7] = 3.3219280948873623;
    cpu_state.tag[cpu_state.TOP & 7] = TAG_VALID;
}

static void
helper_FLDL2E(void)
{
    cpu_state.TOP--;
    cpu_state.ST[cpu_state.TOP & 7] = 1.4426950408889634;
    cpu_state.tag[cpu_state.TOP & 7] = TAG_VALID;
}

static void
helper_FLDPI(void)
{
    cpu_state.TOP--;
    cpu_state.ST[cpu_state.TOP & 7] = 3.141592653589793;
    cpu_state.tag[cpu_state.TOP & 7] = TAG_VALID;
}

static void
helper_FLDLG2(void)
{
    cpu_state.TOP--;
    cpu_state.ST[cpu_state.TOP & 7] = 0.3010299956639812;
    cpu_state.tag[cpu_state.TOP & 7] = TAG_VALID;
}

static void
helper_FLDLN2(void)
{
    union {
        uint64_t i;
        double   d;
    } u;
    u.i = 0x3fe62e42fefa39f0ULL;
    cpu_state.TOP--;
    cpu_state.ST[cpu_state.TOP & 7] = u.d;
    cpu_state.tag[cpu_state.TOP & 7] = TAG_VALID;
}

uint32_t
ropFLDL2T(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_FP_ENTER(ir);
    uop_CALL_FUNC(ir, helper_FLDL2T);

    return op_pc;
}

uint32_t
ropFLDL2E(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_FP_ENTER(ir);
    uop_CALL_FUNC(ir, helper_FLDL2E);

    return op_pc;
}

uint32_t
ropFLDPI(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_FP_ENTER(ir);
    uop_CALL_FUNC(ir, helper_FLDPI);

    return op_pc;
}

uint32_t
ropFLDLG2(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_FP_ENTER(ir);
    uop_CALL_FUNC(ir, helper_FLDLG2);

    return op_pc;
}

uint32_t
ropFLDLN2(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uop_FP_ENTER(ir);
    uop_CALL_FUNC(ir, helper_FLDLN2);

    return op_pc;
}

/*
 * FLDCW - Load FPU control word from memory.
 * Opcode: D9 /5 (memory form only).
 * Loads the 16-bit control word and updates the rounding mode.
 */
static void
helper_FLDCW_set_rounding(void)
{
    codegen_set_rounding_mode((cpu_state.npxc >> 10) & 3);
}

uint32_t
ropFLDCW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_read(block, ir, target_seg);
    uop_MEM_LOAD_REG(ir, IREG_NPXC, ireg_seg_base(target_seg), IREG_eaaddr);
    uop_CALL_FUNC(ir, helper_FLDCW_set_rounding);

    return op_pc + 1;
}
