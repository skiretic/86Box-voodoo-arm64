#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_ops.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "x86_flags.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_string.h"
#include "codegen_public.h"

static int codegen_rep_movs_common(uint32_t next_pc, uint32_t addr32, uint32_t stride, uint32_t segment_id);
static int codegen_rep_movsb(UNUSED(uint32_t fetchdat));
static int codegen_rep_movsw(UNUSED(uint32_t fetchdat));
static int codegen_rep_movsl(UNUSED(uint32_t fetchdat));
static int codegen_rep_stos_common(uint32_t next_pc, uint32_t addr32, uint32_t stride);
static int codegen_rep_stosb(UNUSED(uint32_t fetchdat));
static int codegen_rep_stosw(UNUSED(uint32_t fetchdat));
static int codegen_rep_stosl(UNUSED(uint32_t fetchdat));
static int codegen_rep_cmps_common(uint32_t next_pc, uint32_t addr32, int fv, uint32_t segment_id, uint32_t stride);
static int codegen_repe_cmpsb(UNUSED(uint32_t fetchdat));
static int codegen_repne_cmpsb(UNUSED(uint32_t fetchdat));
static int codegen_repe_cmpsw(UNUSED(uint32_t fetchdat));
static int codegen_repe_cmpsl(UNUSED(uint32_t fetchdat));
static int codegen_repne_cmpsw(UNUSED(uint32_t fetchdat));
static int codegen_repne_cmpsl(UNUSED(uint32_t fetchdat));
static int codegen_rep_scasb_common(uint32_t next_pc, uint32_t addr32, int fv);
static int codegen_repe_scasb(UNUSED(uint32_t fetchdat));
static int codegen_repne_scasb(UNUSED(uint32_t fetchdat));
static int codegen_debug_rep_scas_repe(uint32_t fetchdat);
static int codegen_debug_rep_scas_repne(uint32_t fetchdat);

enum {
    CODEGEN_STRING_SEG_DS = 0,
    CODEGEN_STRING_SEG_ES,
    CODEGEN_STRING_SEG_FS,
    CODEGEN_STRING_SEG_GS,
    CODEGEN_STRING_SEG_SS,
    CODEGEN_STRING_SEG_CS,
};

static int
codegen_string_addr_reg(ir_data_t *ir, int addr32, int index_reg)
{
    if (addr32)
        return index_reg;

    uop_MOVZX(ir, IREG_eaaddr, index_reg);
    return IREG_eaaddr;
}

static uint32_t
codegen_string_segment_id(x86seg *seg)
{
    if (seg == &cpu_state.seg_es)
        return CODEGEN_STRING_SEG_ES;
    if (seg == &cpu_state.seg_fs)
        return CODEGEN_STRING_SEG_FS;
    if (seg == &cpu_state.seg_gs)
        return CODEGEN_STRING_SEG_GS;
    if (seg == &cpu_state.seg_ss)
        return CODEGEN_STRING_SEG_SS;
    if (seg == &cpu_state.seg_cs)
        return CODEGEN_STRING_SEG_CS;
    return CODEGEN_STRING_SEG_DS;
}

static x86seg *
codegen_string_segment_from_id(uint32_t segment_id)
{
    switch (segment_id) {
        case CODEGEN_STRING_SEG_ES:
            return &cpu_state.seg_es;
        case CODEGEN_STRING_SEG_FS:
            return &cpu_state.seg_fs;
        case CODEGEN_STRING_SEG_GS:
            return &cpu_state.seg_gs;
        case CODEGEN_STRING_SEG_SS:
            return &cpu_state.seg_ss;
        case CODEGEN_STRING_SEG_CS:
            return &cpu_state.seg_cs;
        case CODEGEN_STRING_SEG_DS:
        default:
            return &cpu_state.seg_ds;
    }
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

static uint32_t
ropCMPS_common(codeblock_t *block, ir_data_t *ir, int src_value_reg, int dst_value_reg, int stride, uint32_t op_32, uint32_t op_pc)
{
    const int addr32        = (op_32 & 0x200) ? 1 : 0;
    const int src_index_reg = addr32 ? IREG_ESI : IREG_SI;
    const int dst_index_reg = addr32 ? IREG_EDI : IREG_DI;
    int       src_addr_reg;
    int       dst_addr_reg;

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_read(block, ir, op_ea_seg);
    codegen_check_seg_read(block, ir, &cpu_state.seg_es);

    src_addr_reg = codegen_string_addr_reg(ir, addr32, src_index_reg);
    CHECK_SEG_LIMITS(block, ir, op_ea_seg, src_addr_reg, stride - 1);
    uop_MEM_LOAD_REG(ir, src_value_reg, ireg_seg_base(op_ea_seg), src_addr_reg);

    dst_addr_reg = codegen_string_addr_reg(ir, addr32, dst_index_reg);
    CHECK_SEG_LIMITS(block, ir, &cpu_state.seg_es, dst_addr_reg, stride - 1);
    uop_MEM_LOAD_REG(ir, dst_value_reg, IREG_ES_base, dst_addr_reg);

    switch (stride) {
        case 1:
            uop_MOVZX(ir, IREG_flags_op1, src_value_reg);
            uop_MOVZX(ir, IREG_flags_op2, dst_value_reg);
            uop_SUB(ir, IREG_flags_res_B, src_value_reg, dst_value_reg);
            uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
            break;
        case 2:
            uop_MOVZX(ir, IREG_flags_op1, src_value_reg);
            uop_MOVZX(ir, IREG_flags_op2, dst_value_reg);
            uop_SUB(ir, IREG_flags_res_W, src_value_reg, dst_value_reg);
            uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
            break;
        case 4:
            uop_MOV(ir, IREG_flags_op1, src_value_reg);
            uop_MOV(ir, IREG_flags_op2, dst_value_reg);
            uop_SUB(ir, IREG_flags_res, src_value_reg, dst_value_reg);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
            break;
        default:
            fatal("ropCMPS_common stride %d\n", stride);
    }

    codegen_flags_changed = 1;
    codegen_adjust_string_index(ir, src_index_reg, stride, addr32);
    codegen_adjust_string_index(ir, dst_index_reg, stride, addr32);

    return op_pc;
}

static uint32_t
ropSCAS_common(codeblock_t *block, ir_data_t *ir, int accum_reg, int mem_value_reg, int stride, uint32_t op_32, uint32_t op_pc)
{
    const int addr32    = (op_32 & 0x200) ? 1 : 0;
    const int index_reg = addr32 ? IREG_EDI : IREG_DI;
    int       addr_reg;

    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    codegen_check_seg_read(block, ir, &cpu_state.seg_es);

    addr_reg = codegen_string_addr_reg(ir, addr32, index_reg);
    CHECK_SEG_LIMITS(block, ir, &cpu_state.seg_es, addr_reg, stride - 1);
    uop_MEM_LOAD_REG(ir, mem_value_reg, IREG_ES_base, addr_reg);

    switch (stride) {
        case 1:
            uop_MOVZX(ir, IREG_flags_op1, accum_reg);
            uop_MOVZX(ir, IREG_flags_op2, mem_value_reg);
            uop_SUB(ir, IREG_flags_res_B, accum_reg, mem_value_reg);
            uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_B);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB8);
            break;
        case 2:
            uop_MOVZX(ir, IREG_flags_op1, accum_reg);
            uop_MOVZX(ir, IREG_flags_op2, mem_value_reg);
            uop_SUB(ir, IREG_flags_res_W, accum_reg, mem_value_reg);
            uop_MOVZX(ir, IREG_flags_res, IREG_flags_res_W);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB16);
            break;
        case 4:
            uop_MOV(ir, IREG_flags_op1, accum_reg);
            uop_MOV(ir, IREG_flags_op2, mem_value_reg);
            uop_SUB(ir, IREG_flags_res, accum_reg, mem_value_reg);
            uop_MOV_IMM(ir, IREG_flags_op, FLAGS_SUB32);
            break;
        default:
            fatal("ropSCAS_common stride %d\n", stride);
    }

    codegen_flags_changed = 1;
    codegen_adjust_string_index(ir, index_reg, stride, addr32);

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
ropREP_MOVSB(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uop_MOV_IMM(ir, IREG_pc, op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, codegen_rep_movsb, 0);
    return -1;
}

uint32_t
ropREP_MOVSW(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uop_MOV_IMM(ir, IREG_pc, op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, codegen_rep_movsw, 0);
    return -1;
}

uint32_t
ropREP_MOVSL(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uop_MOV_IMM(ir, IREG_pc, op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, codegen_rep_movsl, 0);
    return -1;
}

uint32_t
ropREP_STOSB(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uop_MOV_IMM(ir, IREG_pc, op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, codegen_rep_stosb, 0);
    return -1;
}

uint32_t
ropREP_STOSW(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uop_MOV_IMM(ir, IREG_pc, op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, codegen_rep_stosw, 0);
    return -1;
}

uint32_t
ropREP_STOSL(UNUSED(codeblock_t *block), ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    uop_MOV_IMM(ir, IREG_pc, op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, codegen_rep_stosl, 0);
    return -1;
}

static uint32_t
ropREP_CMPSB_common(UNUSED(codeblock_t *block), ir_data_t *ir, uint32_t op_32, uint32_t op_pc, int is_repne)
{
    uop_MOV_IMM(ir, IREG_pc, op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, is_repne ? codegen_repne_cmpsb : codegen_repe_cmpsb, 0);
    return -1;
}

uint32_t
ropREPE_CMPSB(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropREP_CMPSB_common(block, ir, op_32, op_pc, 0);
}

uint32_t
ropREPNE_CMPSB(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropREP_CMPSB_common(block, ir, op_32, op_pc, 1);
}

static uint32_t
ropREP_CMPSW_common(UNUSED(codeblock_t *block), ir_data_t *ir, uint32_t op_32, uint32_t op_pc, int is_repne)
{
    uop_MOV_IMM(ir, IREG_pc, op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, is_repne ? codegen_repne_cmpsw : codegen_repe_cmpsw, 0);
    return -1;
}

uint32_t
ropREPE_CMPSW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropREP_CMPSW_common(block, ir, op_32, op_pc, 0);
}

uint32_t
ropREPNE_CMPSW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropREP_CMPSW_common(block, ir, op_32, op_pc, 1);
}

static uint32_t
ropREP_CMPSL_common(UNUSED(codeblock_t *block), ir_data_t *ir, uint32_t op_32, uint32_t op_pc, int is_repne)
{
    uop_MOV_IMM(ir, IREG_pc, op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, is_repne ? codegen_repne_cmpsl : codegen_repe_cmpsl, 0);
    return -1;
}

uint32_t
ropREPE_CMPSL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropREP_CMPSL_common(block, ir, op_32, op_pc, 0);
}

uint32_t
ropREPNE_CMPSL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropREP_CMPSL_common(block, ir, op_32, op_pc, 1);
}

static uint32_t
ropREP_SCASB_common(UNUSED(codeblock_t *block), ir_data_t *ir, uint32_t op_32, uint32_t op_pc, int is_repne)
{
    uop_MOV_IMM(ir, IREG_pc, op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, is_repne ? codegen_repne_scasb : codegen_repe_scasb, 0);
    return -1;
}

uint32_t
ropREPE_SCASB(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropREP_SCASB_common(block, ir, op_32, op_pc, 0);
}

uint32_t
ropREPNE_SCASB(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropREP_SCASB_common(block, ir, op_32, op_pc, 1);
}

static uint32_t
ropDEBUG_REP_SCAS_common(UNUSED(codeblock_t *block), ir_data_t *ir, uint8_t opcode, UNUSED(uint32_t scas_fetchdat),
                         uint32_t op_32, uint32_t op_pc, int is_repne)
{
    if (!new_dynarec_rep_scas_debug_enabled_for_site(cs + op_pc, opcode))
        return 0;

    uop_MOV_IMM(ir, IREG_pc, op_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) op_ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, is_repne ? codegen_debug_rep_scas_repne : codegen_debug_rep_scas_repe, opcode);
    return -1;
}

uint32_t
ropDEBUG_REPE_SCASB(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return ropDEBUG_REP_SCAS_common(block, ir, opcode, fetchdat, op_32, op_pc, 0);
}

uint32_t
ropDEBUG_REPE_SCASW(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return ropDEBUG_REP_SCAS_common(block, ir, opcode, fetchdat, op_32, op_pc, 0);
}

uint32_t
ropDEBUG_REPE_SCASL(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return ropDEBUG_REP_SCAS_common(block, ir, opcode, fetchdat, op_32, op_pc, 0);
}

uint32_t
ropDEBUG_REPNE_SCASB(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return ropDEBUG_REP_SCAS_common(block, ir, opcode, fetchdat, op_32, op_pc, 1);
}

uint32_t
ropDEBUG_REPNE_SCASW(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return ropDEBUG_REP_SCAS_common(block, ir, opcode, fetchdat, op_32, op_pc, 1);
}

uint32_t
ropDEBUG_REPNE_SCASL(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return ropDEBUG_REP_SCAS_common(block, ir, opcode, fetchdat, op_32, op_pc, 1);
}

uint32_t
ropCMPSB(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropCMPS_common(block, ir, IREG_temp0_B, IREG_temp1_B, 1, op_32, op_pc);
}

uint32_t
ropCMPSW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropCMPS_common(block, ir, IREG_temp0_W, IREG_temp1_W, 2, op_32, op_pc);
}

uint32_t
ropCMPSL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropCMPS_common(block, ir, IREG_temp0, IREG_temp1, 4, op_32, op_pc);
}

uint32_t
ropSCASB(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropSCAS_common(block, ir, IREG_AL, IREG_temp0_B, 1, op_32, op_pc);
}

uint32_t
ropSCASW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropSCAS_common(block, ir, IREG_AX, IREG_temp0_W, 2, op_32, op_pc);
}

uint32_t
ropSCASL(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    return ropSCAS_common(block, ir, IREG_EAX, IREG_temp0, 4, op_32, op_pc);
}

static int
codegen_rep_movs_common(uint32_t next_pc, uint32_t addr32, uint32_t stride, uint32_t segment_id)
{
    x86seg   *src_seg;
    uint32_t count;
    uint32_t src_index;
    uint32_t dst_index;
    int      cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);

    rep_op = (stride == 1) ? 0xa4 : 0xa5;
    src_seg = codegen_string_segment_from_id(segment_id);
    if (trap)
        cycles_end = cycles + 1;

    if (addr32) {
        count     = ECX;
        src_index = ESI;
        dst_index = EDI;
    } else {
        count     = CX;
        src_index = SI;
        dst_index = DI;
    }

    if (count > 0) {
        SEG_CHECK_READ(src_seg);
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        if (cpu_state.abrt)
            return cpu_state.pc;
    }

    while (count > 0) {
        if (stride == 1) {
            uint8_t temp;

            addr64 = addr64_2 = 0x00000000;
            CHECK_READ_REP(src_seg, src_index, src_index);
            CHECK_WRITE_REP(&cpu_state.seg_es, dst_index, dst_index);
            high_page = 0;
            do_mmut_rb(src_seg->base, src_index, &addr64);
            if (cpu_state.abrt)
                break;
            do_mmut_wb(es, dst_index, &addr64_2);
            if (cpu_state.abrt)
                break;
            temp = readmemb_n(src_seg->base, src_index, addr64);
            if (cpu_state.abrt)
                return 1;
            writememb_n(es, dst_index, addr64_2, temp);
            if (cpu_state.abrt)
                return 1;
        } else if (stride == 2) {
            uint16_t temp;

            addr64a[0] = addr64a[1] = 0x00000000;
            addr64a_2[0] = addr64a_2[1] = 0x00000000;
            CHECK_READ_REP(src_seg, src_index, src_index + 1UL);
            CHECK_WRITE_REP(&cpu_state.seg_es, dst_index, dst_index + 1UL);
            high_page = 0;
            do_mmut_rw(src_seg->base, src_index, addr64a);
            if (cpu_state.abrt)
                break;
            do_mmut_ww(es, dst_index, addr64a_2);
            if (cpu_state.abrt)
                break;
            temp = readmemw_n(src_seg->base, src_index, addr64a);
            if (cpu_state.abrt)
                return 1;
            writememw_n(es, dst_index, addr64a_2, temp);
            if (cpu_state.abrt)
                return 1;
        } else if (stride == 4) {
            uint32_t temp;

            addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;
            addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;
            CHECK_READ_REP(src_seg, src_index, src_index + 3UL);
            CHECK_WRITE_REP(&cpu_state.seg_es, dst_index, dst_index + 3UL);
            high_page = 0;
            do_mmut_rl(src_seg->base, src_index, addr64a);
            if (cpu_state.abrt)
                break;
            do_mmut_wl(es, dst_index, addr64a_2);
            if (cpu_state.abrt)
                break;
            temp = readmeml_n(src_seg->base, src_index, addr64a);
            if (cpu_state.abrt)
                return 1;
            writememl_n(es, dst_index, addr64a_2, temp);
            if (cpu_state.abrt)
                return 1;
        } else {
            return 1;
        }

        if (cpu_state.flags & D_FLAG) {
            src_index -= stride;
            dst_index -= stride;
        } else {
            src_index += stride;
            dst_index += stride;
        }
        if (!addr32) {
            src_index &= 0xffff;
            dst_index &= 0xffff;
        }
        count--;
        cycles -= is486 ? 3 : 4;
        if (cycles < cycles_end)
            break;
    }

    if (addr32) {
        ECX = count;
        ESI = src_index;
        EDI = dst_index;
    } else {
        CX = count;
        SI = src_index;
        DI = dst_index;
    }

    if (count > 0) {
        CPU_BLOCK_END();
        cpu_state.pc = cpu_state.oldpc;
        return 1;
    }

    if (!cpu_state.abrt)
        cpu_state.pc = next_pc;

    return cpu_state.abrt ? 1 : 0;
}

static int
codegen_rep_stos_common(uint32_t next_pc, uint32_t addr32, uint32_t stride)
{
    uint32_t count;
    uint32_t dst_index;
    int      cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);

    rep_op = (stride == 1) ? 0xaa : 0xab;
    if (trap)
        cycles_end = cycles + 1;

    if (addr32) {
        count     = ECX;
        dst_index = EDI;
    } else {
        count     = CX;
        dst_index = DI;
    }

    if (count > 0) {
        SEG_CHECK_WRITE(&cpu_state.seg_es);
        if (cpu_state.abrt)
            return 1;
    }

    while (count > 0) {
        if (stride == 1) {
            CHECK_WRITE_REP(&cpu_state.seg_es, dst_index, dst_index);
            writememb(es, dst_index, AL);
            if (cpu_state.abrt)
                return 1;
        } else if (stride == 2) {
            CHECK_WRITE_REP(&cpu_state.seg_es, dst_index, dst_index + 1UL);
            writememw(es, dst_index, AX);
            if (cpu_state.abrt)
                return 1;
        } else if (stride == 4) {
            CHECK_WRITE_REP(&cpu_state.seg_es, dst_index, dst_index + 3UL);
            writememl(es, dst_index, EAX);
            if (cpu_state.abrt)
                return 1;
        } else {
            return 1;
        }

        if (cpu_state.flags & D_FLAG)
            dst_index -= stride;
        else
            dst_index += stride;
        if (!addr32)
            dst_index &= 0xffff;
        count--;
        cycles -= is486 ? 3 : 4;
        if (cycles < cycles_end)
            break;
    }

    if (addr32) {
        ECX = count;
        EDI = dst_index;
    } else {
        CX = count;
        DI = dst_index;
    }

    if (count > 0) {
        CPU_BLOCK_END();
        cpu_state.pc = cpu_state.oldpc;
        return 1;
    }

    if (!cpu_state.abrt)
        cpu_state.pc = next_pc;

    return cpu_state.abrt ? 1 : 0;
}

static int
codegen_rep_movsb(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_movs_common(cpu_state.pc, cpu_state.op32 & 0x200, 1,
                                   codegen_string_segment_id(cpu_state.ea_seg));
}

static int
codegen_rep_movsw(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_movs_common(cpu_state.pc, cpu_state.op32 & 0x200, 2,
                                   codegen_string_segment_id(cpu_state.ea_seg));
}

static int
codegen_rep_movsl(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_movs_common(cpu_state.pc, cpu_state.op32 & 0x200, 4,
                                   codegen_string_segment_id(cpu_state.ea_seg));
}

static int
codegen_rep_stosb(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_stos_common(cpu_state.pc, cpu_state.op32 & 0x200, 1);
}

static int
codegen_rep_stosw(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_stos_common(cpu_state.pc, cpu_state.op32 & 0x200, 2);
}

static int
codegen_rep_stosl(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_stos_common(cpu_state.pc, cpu_state.op32 & 0x200, 4);
}

static int
codegen_rep_cmps_common(uint32_t next_pc, uint32_t addr32, int fv, uint32_t segment_id, uint32_t stride)
{
    x86seg   *src_seg;
    uint32_t count;
    uint32_t src_index;
    uint32_t dst_index;
    int      tempz;
    int      cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);

    rep_op = (stride == 1) ? 0xa6 : 0xa7;
    if (trap)
        cycles_end = cycles + 1;

    src_seg = codegen_string_segment_from_id(segment_id);
    if (addr32) {
        count     = ECX;
        src_index = ESI;
        dst_index = EDI;
    } else {
        count     = CX;
        src_index = SI;
        dst_index = DI;
    }

    tempz = fv;
    if ((count > 0) && (fv == tempz)) {
        SEG_CHECK_READ(src_seg);
        SEG_CHECK_READ(&cpu_state.seg_es);
        if (cpu_state.abrt)
            return 1;
    }

    while ((count > 0) && (fv == tempz)) {
        if (stride == 1) {
            uint8_t src;
            uint8_t dst;

            CHECK_READ_REP(src_seg, src_index, src_index);
            CHECK_READ_REP(&cpu_state.seg_es, dst_index, dst_index);
            src = readmemb(src_seg->base, src_index);
            if (cpu_state.abrt)
                break;
            dst = readmemb(es, dst_index);
            if (cpu_state.abrt)
                break;
            setsub8(src, dst);
        } else if (stride == 2) {
            uint16_t src;
            uint16_t dst;

            CHECK_READ_REP(src_seg, src_index, src_index + 1UL);
            CHECK_READ_REP(&cpu_state.seg_es, dst_index, dst_index + 1UL);
            src = readmemw(src_seg->base, src_index);
            if (cpu_state.abrt)
                break;
            dst = readmemw(es, dst_index);
            if (cpu_state.abrt)
                break;
            setsub16(src, dst);
        } else if (stride == 4) {
            uint32_t src;
            uint32_t dst;

            CHECK_READ_REP(src_seg, src_index, src_index + 3UL);
            CHECK_READ_REP(&cpu_state.seg_es, dst_index, dst_index + 3UL);
            src = readmeml(src_seg->base, src_index);
            if (cpu_state.abrt)
                break;
            dst = readmeml(es, dst_index);
            if (cpu_state.abrt)
                break;
            setsub32(src, dst);
        } else {
            return 1;
        }

        tempz = ZF_SET() ? 1 : 0;
        if (cpu_state.flags & D_FLAG) {
            src_index -= stride;
            dst_index -= stride;
        } else {
            src_index += stride;
            dst_index += stride;
        }
        if (!addr32) {
            src_index &= 0xffff;
            dst_index &= 0xffff;
        }
        count--;
        cycles -= is486 ? 5 : 8;
        if (cycles < cycles_end)
            break;
    }

    if (addr32) {
        ECX = count;
        ESI = src_index;
        EDI = dst_index;
    } else {
        CX = count;
        SI = src_index;
        DI = dst_index;
    }

    if ((count > 0) && (fv == tempz)) {
        CPU_BLOCK_END();
        cpu_state.pc = cpu_state.oldpc;
        return 1;
    }

    if (!cpu_state.abrt)
        cpu_state.pc = next_pc;

    return cpu_state.abrt ? 1 : 0;
}

static int
codegen_repe_cmpsb(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_cmps_common(cpu_state.pc, cpu_state.op32 & 0x200, 1,
                                   codegen_string_segment_id(cpu_state.ea_seg), 1);
}

static int
codegen_repne_cmpsb(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_cmps_common(cpu_state.pc, cpu_state.op32 & 0x200, 0,
                                   codegen_string_segment_id(cpu_state.ea_seg), 1);
}

static int
codegen_repe_cmpsw(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_cmps_common(cpu_state.pc, cpu_state.op32 & 0x200, 1,
                                   codegen_string_segment_id(cpu_state.ea_seg), 2);
}

static int
codegen_repe_cmpsl(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_cmps_common(cpu_state.pc, cpu_state.op32 & 0x200, 1,
                                   codegen_string_segment_id(cpu_state.ea_seg), 4);
}

static int
codegen_repne_cmpsw(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_cmps_common(cpu_state.pc, cpu_state.op32 & 0x200, 0,
                                   codegen_string_segment_id(cpu_state.ea_seg), 2);
}

static int
codegen_repne_cmpsl(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_cmps_common(cpu_state.pc, cpu_state.op32 & 0x200, 0,
                                   codegen_string_segment_id(cpu_state.ea_seg), 4);
}

static int
codegen_rep_scasb_common(uint32_t next_pc, uint32_t addr32, int fv)
{
    uint32_t count;
    uint32_t dst_index;
    int      tempz;
    int      cycles_end = cycles - ((is386 && cpu_use_dynarec) ? 1000 : 100);

    rep_op = 0xae;
    if (trap)
        cycles_end = cycles + 1;

    if (addr32) {
        count     = ECX;
        dst_index = EDI;
    } else {
        count     = CX;
        dst_index = DI;
    }

    tempz = fv;
    if ((count > 0) && (fv == tempz)) {
        SEG_CHECK_READ(&cpu_state.seg_es);
        if (cpu_state.abrt)
            return 1;
    }

    while ((count > 0) && (fv == tempz)) {
        uint8_t temp;

        CHECK_READ_REP(&cpu_state.seg_es, dst_index, dst_index);
        temp = readmemb(es, dst_index);
        if (cpu_state.abrt)
            break;
        setsub8(AL, temp);
        tempz = ZF_SET() ? 1 : 0;
        if (cpu_state.flags & D_FLAG)
            dst_index--;
        else
            dst_index++;
        if (!addr32)
            dst_index &= 0xffff;
        count--;
        cycles -= is486 ? 5 : 8;
        if (cycles < cycles_end)
            break;
    }

    if (addr32) {
        ECX = count;
        EDI = dst_index;
    } else {
        CX = count;
        DI = dst_index;
    }

    if ((count > 0) && (fv == tempz)) {
        CPU_BLOCK_END();
        cpu_state.pc = cpu_state.oldpc;
        return 1;
    }

    if (!cpu_state.abrt)
        cpu_state.pc = next_pc;

    return cpu_state.abrt ? 1 : 0;
}

static int
codegen_repe_scasb(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_scasb_common(cpu_state.pc, cpu_state.op32 & 0x200, 1);
}

static int
codegen_repne_scasb(UNUSED(uint32_t fetchdat))
{
    return codegen_rep_scasb_common(cpu_state.pc, cpu_state.op32 & 0x200, 0);
}

static int
codegen_debug_rep_scas_dispatch(uint32_t fetchdat, const OpFn *table)
{
    const uint16_t opcode = (uint16_t) fetchdat;
    const uint16_t table_opcode = (opcode | cpu_state.op32) & 0x3ff;
    OpFn           op_fn;

    new_dynarec_note_rep_scas_debug_site(cs + cpu_state.oldpc, (uint8_t) opcode);
    op_fn = table[table_opcode];
    if (!op_fn)
        return 1;

    return op_fn(0);
}

static int
codegen_debug_rep_scas_repe(uint32_t fetchdat)
{
    return codegen_debug_rep_scas_dispatch(fetchdat, x86_dynarec_opcodes_REPE);
}

static int
codegen_debug_rep_scas_repne(uint32_t fetchdat)
{
    return codegen_debug_rep_scas_dispatch(fetchdat, x86_dynarec_opcodes_REPNE);
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
