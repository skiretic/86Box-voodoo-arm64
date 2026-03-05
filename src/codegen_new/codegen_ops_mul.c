/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          JIT handlers for multi-operand IMUL instructions.
 *
 *          Covers:
 *            - IMUL r, r/m (2-operand, 0F AF)
 *            - IMUL r, r/m, imm (3-operand, 69 and 6B)
 *
 *          The source operand value is passed via cpu_state.flags_op2,
 *          which is flushed to its backing store by the CALL_FUNC
 *          barrier before the helper is called. The destination register
 *          index is passed as function argument 0 via LOAD_FUNC_ARG_IMM.
 *
 *          For 1-operand MUL/IMUL/DIV/IDIV, see codegen_ops_misc.c
 *          (F6/F7 group handlers).
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
#include "codegen_ops_mul.h"

/*
 * IMUL r16, r/m16 (2-operand)
 * dest = (int16_t)dest * (int16_t)src, lower 16 bits kept.
 * CF=OF=1 if result doesn't fit in 16 bits (sign-extended).
 */
static void
helper_IMUL_w_rm(uint32_t dest_reg)
{
    int32_t templ;

    templ = (int32_t) (int16_t) cpu_state.regs[dest_reg].w * (int32_t) (int16_t) (uint16_t) cpu_state.flags_op2;
    cpu_state.regs[dest_reg].w = templ & 0xFFFF;
    flags_rebuild();
    if ((templ >> 15) != 0 && (templ >> 15) != -1)
        cpu_state.flags |= C_FLAG | V_FLAG;
    else
        cpu_state.flags &= ~(C_FLAG | V_FLAG);
}

/*
 * IMUL r32, r/m32 (2-operand)
 */
static void
helper_IMUL_l_rm(uint32_t dest_reg)
{
    int64_t temp64;

    temp64 = (int64_t) (int32_t) cpu_state.regs[dest_reg].l * (int64_t) (int32_t) cpu_state.flags_op2;
    cpu_state.regs[dest_reg].l = temp64 & 0xFFFFFFFF;
    flags_rebuild();
    if ((temp64 >> 31) != 0 && (temp64 >> 31) != -1)
        cpu_state.flags |= C_FLAG | V_FLAG;
    else
        cpu_state.flags &= ~(C_FLAG | V_FLAG);
}

/*
 * IMUL r16, r/m16, imm16 (3-operand)
 * dest = (int16_t)src * (int16_t)imm, lower 16 bits kept.
 */
static void
helper_IMUL_w_rm_imm(uint32_t dest_reg, uint32_t imm)
{
    int32_t templ;

    templ = (int32_t) (int16_t) (uint16_t) cpu_state.flags_op2 * (int32_t) (int16_t) (uint16_t) imm;
    cpu_state.regs[dest_reg].w = templ & 0xFFFF;
    flags_rebuild();
    if ((templ >> 15) != 0 && (templ >> 15) != -1)
        cpu_state.flags |= C_FLAG | V_FLAG;
    else
        cpu_state.flags &= ~(C_FLAG | V_FLAG);
}

/*
 * IMUL r32, r/m32, imm32 (3-operand)
 */
static void
helper_IMUL_l_rm_imm(uint32_t dest_reg, uint32_t imm)
{
    int64_t temp64;

    temp64 = (int64_t) (int32_t) cpu_state.flags_op2 * (int64_t) (int32_t) imm;
    cpu_state.regs[dest_reg].l = temp64 & 0xFFFFFFFF;
    flags_rebuild();
    if ((temp64 >> 31) != 0 && (temp64 >> 31) != -1)
        cpu_state.flags |= C_FLAG | V_FLAG;
    else
        cpu_state.flags &= ~(C_FLAG | V_FLAG);
}

/* ========================================================================
 * IMUL r, r/m (2-operand) -- Opcode 0F AF
 * ======================================================================== */

uint32_t
ropIMUL_w_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
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
    uop_CALL_FUNC(ir, helper_IMUL_w_rm);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

uint32_t
ropIMUL_l_rm(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
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
    uop_CALL_FUNC(ir, helper_IMUL_l_rm);

    codegen_flags_changed = 0;
    return op_pc + 1;
}

/* ========================================================================
 * IMUL r, r/m, imm (3-operand) -- Opcode 69 (imm16/32) and 6B (imm8)
 * ======================================================================== */

uint32_t
ropIMUL_w_rm_iw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int      dest_reg = (fetchdat >> 3) & 7;
    uint16_t imm_data;

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

    imm_data = fastreadw(cs + op_pc + 1);
    codegen_mark_code_present(block, cs + op_pc + 1, 2);

    uop_LOAD_FUNC_ARG_IMM(ir, 0, dest_reg);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, (uint32_t) imm_data);
    uop_CALL_FUNC(ir, helper_IMUL_w_rm_imm);

    codegen_flags_changed = 0;
    return op_pc + 3;
}

uint32_t
ropIMUL_l_rm_il(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int      dest_reg = (fetchdat >> 3) & 7;
    uint32_t imm_data;

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

    imm_data = fastreadl(cs + op_pc + 1);
    codegen_mark_code_present(block, cs + op_pc + 1, 4);

    uop_LOAD_FUNC_ARG_IMM(ir, 0, dest_reg);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, imm_data);
    uop_CALL_FUNC(ir, helper_IMUL_l_rm_imm);

    codegen_flags_changed = 0;
    return op_pc + 5;
}

uint32_t
ropIMUL_w_rm_ib(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int      dest_reg = (fetchdat >> 3) & 7;
    uint16_t imm_data;

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

    imm_data = fastreadb(cs + op_pc + 1);
    if (imm_data & 0x80)
        imm_data |= 0xff00;
    codegen_mark_code_present(block, cs + op_pc + 1, 1);

    uop_LOAD_FUNC_ARG_IMM(ir, 0, dest_reg);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, (uint32_t) imm_data);
    uop_CALL_FUNC(ir, helper_IMUL_w_rm_imm);

    codegen_flags_changed = 0;
    return op_pc + 2;
}

uint32_t
ropIMUL_l_rm_ib(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int      dest_reg = (fetchdat >> 3) & 7;
    uint32_t imm_data;

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

    imm_data = fastreadb(cs + op_pc + 1);
    if (imm_data & 0x80)
        imm_data |= 0xffffff00;
    codegen_mark_code_present(block, cs + op_pc + 1, 1);

    uop_LOAD_FUNC_ARG_IMM(ir, 0, dest_reg);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, imm_data);
    uop_CALL_FUNC(ir, helper_IMUL_l_rm_imm);

    codegen_flags_changed = 0;
    return op_pc + 2;
}
