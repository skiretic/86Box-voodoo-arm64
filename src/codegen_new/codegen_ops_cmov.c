/*
 * 86Box    A hypervisor and target emulation engine for x86-class PCs.
 *
 *          CMOVcc (Conditional Move) JIT handlers.
 *
 *          Translates 0F 40-4F opcodes into UOP sequences.
 *          Each condition has a 16-bit and 32-bit variant.
 *          The source operand (reg or memory) is loaded into
 *          IREG_flags_op2, and a helper function conditionally
 *          copies it into the destination register.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
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
#include "codegen.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_cmov.h"

/*
 * CMOVcc helper functions.
 *
 * Convention: the source operand value is passed in cpu_state.flags_op2.
 * The CALL_FUNC barrier flushes all IREGs to cpu_state, so the helper
 * can read flags_op2 directly. The dest_reg index is passed as a
 * function argument.
 *
 * CMOVcc does NOT modify flags — each helper only conditionally copies
 * the source value to the destination register.
 */

/* clang-format off */

/*
 * Macro to define a pair of CMOVcc helpers (16-bit and 32-bit)
 * for a given condition.
 */
#define CMOV_HELPERS(cond, test)                                           \
    static void                                                            \
    helper_CMOV##cond##_w(uint32_t dest_reg)                               \
    {                                                                      \
        if (test)                                                          \
            cpu_state.regs[dest_reg].w = (uint16_t) cpu_state.flags_op2;   \
    }                                                                      \
    static void                                                            \
    helper_CMOV##cond##_l(uint32_t dest_reg)                               \
    {                                                                      \
        if (test)                                                          \
            cpu_state.regs[dest_reg].l = cpu_state.flags_op2;              \
    }

CMOV_HELPERS(O,   VF_SET())
CMOV_HELPERS(NO,  !VF_SET())
CMOV_HELPERS(B,   CF_SET())
CMOV_HELPERS(NB,  !CF_SET())
CMOV_HELPERS(E,   ZF_SET())
CMOV_HELPERS(NE,  !ZF_SET())
CMOV_HELPERS(BE,  CF_SET() || ZF_SET())
CMOV_HELPERS(NBE, !CF_SET() && !ZF_SET())
CMOV_HELPERS(S,   NF_SET())
CMOV_HELPERS(NS,  !NF_SET())
CMOV_HELPERS(P,   PF_SET())
CMOV_HELPERS(NP,  !PF_SET())
CMOV_HELPERS(L,   ((NF_SET() ? 1 : 0) != (VF_SET() ? 1 : 0)))
CMOV_HELPERS(NL,  ((NF_SET() ? 1 : 0) == (VF_SET() ? 1 : 0)))
CMOV_HELPERS(LE,  ((NF_SET() ? 1 : 0) != (VF_SET() ? 1 : 0)) || ZF_SET())
CMOV_HELPERS(NLE, ((NF_SET() ? 1 : 0) == (VF_SET() ? 1 : 0)) && !ZF_SET())

/* clang-format on */

/*
 * Common rop handler for CMOVcc (16-bit operand size).
 *
 * Loads the source operand from register or memory into IREG_flags_op2,
 * then calls the condition-specific helper which conditionally writes
 * the value to the destination register.
 */
static uint32_t
ropCMOVcc_common_w(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, void *helper_func)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVZX(ir, IREG_flags_op2, IREG_16(src_reg));
    } else {
        x86seg *target_seg;
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_flags_op2, IREG_temp0_W);
    }

    uop_LOAD_FUNC_ARG_IMM(ir, 0, dest_reg);
    uop_CALL_FUNC(ir, helper_func);

    return op_pc + 1;
}

/*
 * Common rop handler for CMOVcc (32-bit operand size).
 */
static uint32_t
ropCMOVcc_common_l(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, void *helper_func)
{
    int dest_reg = (fetchdat >> 3) & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOV(ir, IREG_flags_op2, IREG_32(src_reg));
    } else {
        x86seg *target_seg;
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_flags_op2, IREG_temp0);
    }

    uop_LOAD_FUNC_ARG_IMM(ir, 0, dest_reg);
    uop_CALL_FUNC(ir, helper_func);

    return op_pc + 1;
}

/*
 * CMOVcc rop handler wrappers — one per condition per size.
 * These are the public functions wired into the dispatch table.
 */

/* clang-format off */
#define CMOV_ROP_W(cond) \
    uint32_t ropCMOV##cond##_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
    { return ropCMOVcc_common_w(block, ir, fetchdat, op_32, op_pc, helper_CMOV##cond##_w); }

#define CMOV_ROP_L(cond) \
    uint32_t ropCMOV##cond##_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc) \
    { return ropCMOVcc_common_l(block, ir, fetchdat, op_32, op_pc, helper_CMOV##cond##_l); }

#define CMOV_ROPS(cond) \
    CMOV_ROP_W(cond)    \
    CMOV_ROP_L(cond)

CMOV_ROPS(O)
CMOV_ROPS(NO)
CMOV_ROPS(B)
CMOV_ROPS(NB)
CMOV_ROPS(E)
CMOV_ROPS(NE)
CMOV_ROPS(BE)
CMOV_ROPS(NBE)
CMOV_ROPS(S)
CMOV_ROPS(NS)
CMOV_ROPS(P)
CMOV_ROPS(NP)
CMOV_ROPS(L)
CMOV_ROPS(NL)
CMOV_ROPS(LE)
CMOV_ROPS(NLE)
/* clang-format on */
