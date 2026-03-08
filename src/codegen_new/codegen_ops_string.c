#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "x86_flags.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_string.h"

static int
codegen_string_addr_reg(ir_data_t *ir, int addr32, int index_reg)
{
    if (addr32)
        return index_reg;

    uop_MOVZX(ir, IREG_eaaddr, index_reg);
    return IREG_eaaddr;
}

static void
codegen_adjust_string_index(ir_data_t *ir, int index_reg, int stride, int addr32)
{
    if (addr32) {
        uop_AND_IMM(ir, IREG_temp0_W, IREG_flags, D_FLAG);
        uop_SHR_IMM(ir, IREG_temp0_W, IREG_temp0_W, 9);
        if (stride > 1)
            uop_SHL_IMM(ir, IREG_temp0_W, IREG_temp0_W, stride == 2 ? 1 : 2);
        uop_MOVZX(ir, IREG_temp0, IREG_temp0_W);
        uop_MOV_IMM(ir, IREG_temp1, stride);
        uop_SUB(ir, IREG_temp1, IREG_temp1, IREG_temp0);
        uop_ADD(ir, index_reg, index_reg, IREG_temp1);
    } else {
        uop_AND_IMM(ir, IREG_temp0_W, IREG_flags, D_FLAG);
        uop_SHR_IMM(ir, IREG_temp0_W, IREG_temp0_W, 9);
        if (stride > 1)
            uop_SHL_IMM(ir, IREG_temp0_W, IREG_temp0_W, stride == 2 ? 1 : 2);
        uop_MOV_IMM(ir, IREG_temp1_W, stride);
        uop_SUB(ir, IREG_temp1_W, IREG_temp1_W, IREG_temp0_W);
        uop_ADD(ir, index_reg, index_reg, IREG_temp1_W);
    }
}

static uint32_t
ropSTOS_common(codeblock_t *block, ir_data_t *ir, int value_reg, int stride, uint32_t op_32, uint32_t op_pc)
{
    const int addr32    = (op_32 & 0x200) ? 1 : 0;
    const int index_reg = addr32 ? IREG_EDI : IREG_DI;
    int       addr_reg;

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_write(block, ir, &cpu_state.seg_es);

    addr_reg = codegen_string_addr_reg(ir, addr32, index_reg);
    CHECK_SEG_LIMITS(block, ir, &cpu_state.seg_es, addr_reg, stride - 1);
    uop_MEM_STORE_REG(ir, IREG_ES_base, addr_reg, value_reg);
    codegen_adjust_string_index(ir, index_reg, stride, addr32);

    return op_pc;
}

static uint32_t
ropLODS_common(codeblock_t *block, ir_data_t *ir, int value_reg, int stride, uint32_t op_32, uint32_t op_pc)
{
    const int addr32    = (op_32 & 0x200) ? 1 : 0;
    const int index_reg = addr32 ? IREG_ESI : IREG_SI;
    int       addr_reg;

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_read(block, ir, op_ea_seg);

    addr_reg = codegen_string_addr_reg(ir, addr32, index_reg);
    CHECK_SEG_LIMITS(block, ir, op_ea_seg, addr_reg, stride - 1);
    uop_MEM_LOAD_REG(ir, value_reg, ireg_seg_base(op_ea_seg), addr_reg);
    codegen_adjust_string_index(ir, index_reg, stride, addr32);

    return op_pc;
}

static uint32_t
ropMOVS_common(codeblock_t *block, ir_data_t *ir, int value_reg, int stride, uint32_t op_32, uint32_t op_pc)
{
    const int src_addr32    = (op_32 & 0x200) ? 1 : 0;
    const int src_index_reg = src_addr32 ? IREG_ESI : IREG_SI;
    const int dst_index_reg = src_addr32 ? IREG_EDI : IREG_DI;
    int       src_addr_reg;
    int       dst_addr_reg;

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_read(block, ir, op_ea_seg);
    codegen_check_seg_write(block, ir, &cpu_state.seg_es);

    src_addr_reg = codegen_string_addr_reg(ir, src_addr32, src_index_reg);
    CHECK_SEG_LIMITS(block, ir, op_ea_seg, src_addr_reg, stride - 1);
    uop_MEM_LOAD_REG(ir, value_reg, ireg_seg_base(op_ea_seg), src_addr_reg);

    dst_addr_reg = codegen_string_addr_reg(ir, src_addr32, dst_index_reg);
    CHECK_SEG_LIMITS(block, ir, &cpu_state.seg_es, dst_addr_reg, stride - 1);
    uop_MEM_STORE_REG(ir, IREG_ES_base, dst_addr_reg, value_reg);

    codegen_adjust_string_index(ir, src_index_reg, stride, src_addr32);
    codegen_adjust_string_index(ir, dst_index_reg, stride, src_addr32);

    return op_pc;
}

uint32_t
ropSTOSB(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropSTOS_common(block, ir, IREG_AL, 1, op_32, op_pc);
}

uint32_t
ropSTOSW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropSTOS_common(block, ir, IREG_AX, 2, op_32, op_pc);
}

uint32_t
ropSTOSL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropSTOS_common(block, ir, IREG_EAX, 4, op_32, op_pc);
}

uint32_t
ropLODSB(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropLODS_common(block, ir, IREG_AL, 1, op_32, op_pc);
}

uint32_t
ropMOVSB(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropMOVS_common(block, ir, IREG_temp0_B, 1, op_32, op_pc);
}

uint32_t
ropMOVSW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropMOVS_common(block, ir, IREG_temp0_W, 2, op_32, op_pc);
}

uint32_t
ropMOVSL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropMOVS_common(block, ir, IREG_temp0, 4, op_32, op_pc);
}

uint32_t
ropLODSW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropLODS_common(block, ir, IREG_AX, 2, op_32, op_pc);
}

uint32_t
ropLODSL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropLODS_common(block, ir, IREG_EAX, 4, op_32, op_pc);
}
