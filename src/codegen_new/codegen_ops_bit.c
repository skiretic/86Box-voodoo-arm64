/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          JIT handlers for bit test (BT/BTS/BTR/BTC) and bit scan
 *          (BSF/BSR) instructions.
 *
 *          BT/BTS/BTR/BTC: register-register forms only. Memory
 *          operand forms have complex bit-offset addressing (the bit
 *          index can extend beyond the addressed word) and fall through
 *          to the interpreter.
 *
 *          BSF/BSR: both register and memory source forms are handled
 *          since they only read the source operand.
 *
 *          The source value is passed via cpu_state.flags_op2, which is
 *          flushed by the CALL_FUNC barrier. The register index is
 *          passed as function argument 0 via LOAD_FUNC_ARG_IMM.
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
#include "codegen_ops_bit.h"

/*
 * BT helpers — test a bit and set/clear CF accordingly.
 * The source value is in cpu_state.flags_op2, and the bit index
 * register number is passed as the function argument.
 *
 * For register-register BT, the bit index is masked to operand size.
 */
static void
helper_BT_w(uint32_t bit_reg)
{
    uint16_t src = (uint16_t) cpu_state.flags_op2;
    uint16_t bit = cpu_state.regs[bit_reg].w & 15;

    flags_rebuild();
    if (src & (1 << bit))
        cpu_state.flags |= C_FLAG;
    else
        cpu_state.flags &= ~C_FLAG;
}

static void
helper_BT_l(uint32_t bit_reg)
{
    uint32_t src = cpu_state.flags_op2;
    uint32_t bit = cpu_state.regs[bit_reg].l & 31;

    flags_rebuild();
    if (src & (1u << bit))
        cpu_state.flags |= C_FLAG;
    else
        cpu_state.flags &= ~C_FLAG;
}

/*
 * BTS helpers — test bit, set CF = old bit, then SET the bit.
 * The modified value is written back to cpu_state.flags_op2 so the
 * caller can use it for register writeback.
 */
static void
helper_BTS_w(uint32_t rm_reg)
{
    uint16_t src = cpu_state.regs[rm_reg].w;
    uint16_t bit = (uint16_t) cpu_state.flags_op2 & 15;

    flags_rebuild();
    if (src & (1 << bit))
        cpu_state.flags |= C_FLAG;
    else
        cpu_state.flags &= ~C_FLAG;

    cpu_state.regs[rm_reg].w = src | (1 << bit);
}

static void
helper_BTS_l(uint32_t rm_reg)
{
    uint32_t src = cpu_state.regs[rm_reg].l;
    uint32_t bit = cpu_state.flags_op2 & 31;

    flags_rebuild();
    if (src & (1u << bit))
        cpu_state.flags |= C_FLAG;
    else
        cpu_state.flags &= ~C_FLAG;

    cpu_state.regs[rm_reg].l = src | (1u << bit);
}

/*
 * BTR helpers — test bit, set CF = old bit, then CLEAR the bit.
 */
static void
helper_BTR_w(uint32_t rm_reg)
{
    uint16_t src = cpu_state.regs[rm_reg].w;
    uint16_t bit = (uint16_t) cpu_state.flags_op2 & 15;

    flags_rebuild();
    if (src & (1 << bit))
        cpu_state.flags |= C_FLAG;
    else
        cpu_state.flags &= ~C_FLAG;

    cpu_state.regs[rm_reg].w = src & ~(1 << bit);
}

static void
helper_BTR_l(uint32_t rm_reg)
{
    uint32_t src = cpu_state.regs[rm_reg].l;
    uint32_t bit = cpu_state.flags_op2 & 31;

    flags_rebuild();
    if (src & (1u << bit))
        cpu_state.flags |= C_FLAG;
    else
        cpu_state.flags &= ~C_FLAG;

    cpu_state.regs[rm_reg].l = src & ~(1u << bit);
}

/*
 * BTC helpers — test bit, set CF = old bit, then COMPLEMENT the bit.
 */
static void
helper_BTC_w(uint32_t rm_reg)
{
    uint16_t src = cpu_state.regs[rm_reg].w;
    uint16_t bit = (uint16_t) cpu_state.flags_op2 & 15;

    flags_rebuild();
    if (src & (1 << bit))
        cpu_state.flags |= C_FLAG;
    else
        cpu_state.flags &= ~C_FLAG;

    cpu_state.regs[rm_reg].w = src ^ (1 << bit);
}

static void
helper_BTC_l(uint32_t rm_reg)
{
    uint32_t src = cpu_state.regs[rm_reg].l;
    uint32_t bit = cpu_state.flags_op2 & 31;

    flags_rebuild();
    if (src & (1u << bit))
        cpu_state.flags |= C_FLAG;
    else
        cpu_state.flags &= ~C_FLAG;

    cpu_state.regs[rm_reg].l = src ^ (1u << bit);
}

/*
 * BSF helpers — scan forward (bit 0 upward) for the first set bit.
 * Source is in cpu_state.flags_op2. If zero, ZF=1 and dest unchanged.
 * Otherwise ZF=0 and dest = index of lowest set bit.
 */
static void
helper_BSF_w(uint32_t dest_reg)
{
    uint16_t src = (uint16_t) cpu_state.flags_op2;

    flags_rebuild();
    if (src == 0) {
        cpu_state.flags |= Z_FLAG;
    } else {
        int bit;

        cpu_state.flags &= ~Z_FLAG;
        for (bit = 0; bit < 16; bit++) {
            if (src & (1 << bit)) {
                cpu_state.regs[dest_reg].w = bit;
                break;
            }
        }
    }
}

static void
helper_BSF_l(uint32_t dest_reg)
{
    uint32_t src = cpu_state.flags_op2;

    flags_rebuild();
    if (src == 0) {
        cpu_state.flags |= Z_FLAG;
    } else {
        int bit;

        cpu_state.flags &= ~Z_FLAG;
        for (bit = 0; bit < 32; bit++) {
            if (src & (1u << bit)) {
                cpu_state.regs[dest_reg].l = bit;
                break;
            }
        }
    }
}

/*
 * BSR helpers — scan reverse (MSB downward) for the first set bit.
 * Source is in cpu_state.flags_op2. If zero, ZF=1 and dest unchanged.
 * Otherwise ZF=0 and dest = index of highest set bit.
 */
static void
helper_BSR_w(uint32_t dest_reg)
{
    uint16_t src = (uint16_t) cpu_state.flags_op2;

    flags_rebuild();
    if (src == 0) {
        cpu_state.flags |= Z_FLAG;
    } else {
        int bit;

        cpu_state.flags &= ~Z_FLAG;
        for (bit = 15; bit >= 0; bit--) {
            if (src & (1 << bit)) {
                cpu_state.regs[dest_reg].w = bit;
                break;
            }
        }
    }
}

static void
helper_BSR_l(uint32_t dest_reg)
{
    uint32_t src = cpu_state.flags_op2;

    flags_rebuild();
    if (src == 0) {
        cpu_state.flags |= Z_FLAG;
    } else {
        int bit;

        cpu_state.flags &= ~Z_FLAG;
        for (bit = 31; bit >= 0; bit--) {
            if (src & (1u << bit)) {
                cpu_state.regs[dest_reg].l = bit;
                break;
            }
        }
    }
}

/* ======================================================================
 * BT r/m16, r16 (0F A3) — register-register only
 * ====================================================================== */
uint32_t
ropBT_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int bit_reg = (fetchdat >> 3) & 7;
    int rm_reg  = fetchdat & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);

    /* Register-register only; memory form has complex bit-offset addressing */
    if ((fetchdat & 0xc0) != 0xc0)
        return 0;

    /* Load r/m value into flags_op2 for the helper */
    uop_MOVZX(ir, IREG_flags_op2, IREG_16(rm_reg));

    uop_LOAD_FUNC_ARG_IMM(ir, 0, bit_reg);
    uop_CALL_FUNC(ir, helper_BT_w);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* BT r/m32, r32 (0F A3) — register-register only */
uint32_t
ropBT_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int bit_reg = (fetchdat >> 3) & 7;
    int rm_reg  = fetchdat & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);

    if ((fetchdat & 0xc0) != 0xc0)
        return 0;

    uop_MOV(ir, IREG_flags_op2, IREG_32(rm_reg));

    uop_LOAD_FUNC_ARG_IMM(ir, 0, bit_reg);
    uop_CALL_FUNC(ir, helper_BT_l);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* ======================================================================
 * BTS r/m16, r16 (0F AB) — register-register only
 * ====================================================================== */
uint32_t
ropBTS_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int bit_reg = (fetchdat >> 3) & 7;
    int rm_reg  = fetchdat & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);

    if ((fetchdat & 0xc0) != 0xc0)
        return 0;

    /* Pass bit index via flags_op2, rm register index via arg */
    uop_MOVZX(ir, IREG_flags_op2, IREG_16(bit_reg));

    uop_LOAD_FUNC_ARG_IMM(ir, 0, rm_reg);
    uop_CALL_FUNC(ir, helper_BTS_w);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* BTS r/m32, r32 (0F AB) — register-register only */
uint32_t
ropBTS_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int bit_reg = (fetchdat >> 3) & 7;
    int rm_reg  = fetchdat & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);

    if ((fetchdat & 0xc0) != 0xc0)
        return 0;

    uop_MOV(ir, IREG_flags_op2, IREG_32(bit_reg));

    uop_LOAD_FUNC_ARG_IMM(ir, 0, rm_reg);
    uop_CALL_FUNC(ir, helper_BTS_l);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* ======================================================================
 * BTR r/m16, r16 (0F B3) — register-register only
 * ====================================================================== */
uint32_t
ropBTR_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int bit_reg = (fetchdat >> 3) & 7;
    int rm_reg  = fetchdat & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);

    if ((fetchdat & 0xc0) != 0xc0)
        return 0;

    uop_MOVZX(ir, IREG_flags_op2, IREG_16(bit_reg));

    uop_LOAD_FUNC_ARG_IMM(ir, 0, rm_reg);
    uop_CALL_FUNC(ir, helper_BTR_w);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* BTR r/m32, r32 (0F B3) — register-register only */
uint32_t
ropBTR_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int bit_reg = (fetchdat >> 3) & 7;
    int rm_reg  = fetchdat & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);

    if ((fetchdat & 0xc0) != 0xc0)
        return 0;

    uop_MOV(ir, IREG_flags_op2, IREG_32(bit_reg));

    uop_LOAD_FUNC_ARG_IMM(ir, 0, rm_reg);
    uop_CALL_FUNC(ir, helper_BTR_l);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* ======================================================================
 * BTC r/m16, r16 (0F BB) — register-register only
 * ====================================================================== */
uint32_t
ropBTC_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int bit_reg = (fetchdat >> 3) & 7;
    int rm_reg  = fetchdat & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);

    if ((fetchdat & 0xc0) != 0xc0)
        return 0;

    uop_MOVZX(ir, IREG_flags_op2, IREG_16(bit_reg));

    uop_LOAD_FUNC_ARG_IMM(ir, 0, rm_reg);
    uop_CALL_FUNC(ir, helper_BTC_w);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* BTC r/m32, r32 (0F BB) — register-register only */
uint32_t
ropBTC_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    int bit_reg = (fetchdat >> 3) & 7;
    int rm_reg  = fetchdat & 7;

    codegen_mark_code_present(block, cs + op_pc, 1);

    if ((fetchdat & 0xc0) != 0xc0)
        return 0;

    uop_MOV(ir, IREG_flags_op2, IREG_32(bit_reg));

    uop_LOAD_FUNC_ARG_IMM(ir, 0, rm_reg);
    uop_CALL_FUNC(ir, helper_BTC_l);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* ======================================================================
 * BSF r16, r/m16 (0F BC)
 * Both register and memory source forms are supported since BSF only
 * reads the source operand.
 * ====================================================================== */
uint32_t
ropBSF_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
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
    uop_CALL_FUNC(ir, helper_BSF_w);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* BSF r32, r/m32 (0F BC) */
uint32_t
ropBSF_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
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
    uop_CALL_FUNC(ir, helper_BSF_l);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* ======================================================================
 * BSR r16, r/m16 (0F BD)
 * ====================================================================== */
uint32_t
ropBSR_w(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
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
    uop_CALL_FUNC(ir, helper_BSR_w);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* BSR r32, r/m32 (0F BD) */
uint32_t
ropBSR_l(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
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
    uop_CALL_FUNC(ir, helper_BSR_l);

    codegen_flags_changed = 0;
    return op_pc + 1;
}
