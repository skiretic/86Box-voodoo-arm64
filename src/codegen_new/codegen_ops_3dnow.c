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
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_3dnow.h"
#include "codegen_ops_helpers.h"

static uint64_t dynarec_3dnow_op_total;
static uint64_t dynarec_3dnow_op_recip;
static uint64_t dynarec_3dnow_op_shuffle_pack;
static uint64_t dynarec_3dnow_op_arith;
static uint64_t dynarec_3dnow_op_cmp;
static uint64_t dynarec_3dnow_op_conv;
static uint64_t dynarec_3dnow_op_other;
static uint64_t dynarec_3dnow_op_pfrcp;
static uint64_t dynarec_3dnow_op_pfrsqrt;
static uint64_t dynarec_3dnow_op_pfnacc;
static uint64_t dynarec_3dnow_op_pfpnacc;
static uint64_t dynarec_3dnow_op_pswapd;
static uint64_t dynarec_3dnow_op_pi2fw;
static uint64_t dynarec_3dnow_op_pfadd;
static uint64_t dynarec_3dnow_op_pfsub;
static uint64_t dynarec_3dnow_op_pfsubr;
static uint64_t dynarec_3dnow_op_pfmul;
static uint64_t dynarec_3dnow_op_pfacc;
static uint64_t dynarec_3dnow_op_pavgusb;

static void
dynarec_3dnow_cov_count_op(uint8_t opcode)
{
    dynarec_3dnow_op_total++;

    switch (opcode) {
        case 0x96:
            dynarec_3dnow_op_recip++;
            dynarec_3dnow_op_pfrcp++;
            break;
        case 0x97:
            dynarec_3dnow_op_recip++;
            dynarec_3dnow_op_pfrsqrt++;
            break;
        case 0xa6:
        case 0xa7:
        case 0xb6:
            dynarec_3dnow_op_recip++;
            break;
        case 0x8a:
            dynarec_3dnow_op_shuffle_pack++;
            dynarec_3dnow_op_pfnacc++;
            break;
        case 0x8e:
            dynarec_3dnow_op_shuffle_pack++;
            dynarec_3dnow_op_pfpnacc++;
            break;
        case 0xbb:
            dynarec_3dnow_op_shuffle_pack++;
            dynarec_3dnow_op_pswapd++;
            break;
        case 0x0c:
            dynarec_3dnow_op_conv++;
            dynarec_3dnow_op_pi2fw++;
            break;
        case 0x0d:
        case 0x1c:
        case 0x1d:
            dynarec_3dnow_op_conv++;
            break;
        case 0x9a:
            dynarec_3dnow_op_pfsub++;
            dynarec_3dnow_op_arith++;
            break;
        case 0x9e:
            dynarec_3dnow_op_pfadd++;
            dynarec_3dnow_op_arith++;
            break;
        case 0xaa:
            dynarec_3dnow_op_pfsubr++;
            dynarec_3dnow_op_arith++;
            break;
        case 0xae:
            dynarec_3dnow_op_pfacc++;
            dynarec_3dnow_op_arith++;
            break;
        case 0xb4:
            dynarec_3dnow_op_pfmul++;
            dynarec_3dnow_op_arith++;
            break;
        case 0xbf:
            dynarec_3dnow_op_pavgusb++;
            dynarec_3dnow_op_arith++;
            break;
        case 0x90:
        case 0x94:
        case 0xa0:
        case 0xa4:
        case 0xb0:
            dynarec_3dnow_op_cmp++;
            break;
        default:
            dynarec_3dnow_op_other++;
            break;
    }
}

void
dynarec_3dnow_cov_log_op_summary(void)
{
    if (!dynarec_3dnow_op_total)
        return;

    pclog("DYNAREC_3DNOW_OPSUMMARY total=%llu recip=%llu shuffle_pack=%llu arith=%llu cmp=%llu conv=%llu other=%llu pfrcp=%llu pfrsqrt=%llu pfnacc=%llu pfpnacc=%llu pswapd=%llu pi2fw=%llu pfadd=%llu pfsub=%llu pfsubr=%llu pfmul=%llu pfacc=%llu pavgusb=%llu\n",
          (unsigned long long) dynarec_3dnow_op_total,
          (unsigned long long) dynarec_3dnow_op_recip,
          (unsigned long long) dynarec_3dnow_op_shuffle_pack,
          (unsigned long long) dynarec_3dnow_op_arith,
          (unsigned long long) dynarec_3dnow_op_cmp,
          (unsigned long long) dynarec_3dnow_op_conv,
          (unsigned long long) dynarec_3dnow_op_other,
          (unsigned long long) dynarec_3dnow_op_pfrcp,
          (unsigned long long) dynarec_3dnow_op_pfrsqrt,
          (unsigned long long) dynarec_3dnow_op_pfnacc,
          (unsigned long long) dynarec_3dnow_op_pfpnacc,
          (unsigned long long) dynarec_3dnow_op_pswapd,
          (unsigned long long) dynarec_3dnow_op_pi2fw,
          (unsigned long long) dynarec_3dnow_op_pfadd,
          (unsigned long long) dynarec_3dnow_op_pfsub,
          (unsigned long long) dynarec_3dnow_op_pfsubr,
          (unsigned long long) dynarec_3dnow_op_pfmul,
          (unsigned long long) dynarec_3dnow_op_pfacc,
          (unsigned long long) dynarec_3dnow_op_pavgusb);
}

#define ropParith(func, opid)                                                                      \
    uint32_t rop##func(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),                  \
                       uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                          \
    {                                                                                              \
        dynarec_3dnow_cov_count_op(opid);                                                           \
        int dest_reg = (fetchdat >> 3) & 7;                                                        \
                                                                                                   \
        uop_MMX_ENTER(ir);                                                                         \
        codegen_mark_code_present(block, cs + op_pc, 1);                                           \
        if ((fetchdat & 0xc0) == 0xc0) {                                                           \
            int src_reg = fetchdat & 7;                                                            \
            uop_##func(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_MM(src_reg));                \
        } else {                                                                                   \
            x86seg *target_seg;                                                                    \
                                                                                                   \
            uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
            target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
            codegen_check_seg_read(block, ir, target_seg);                                         \
            uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);            \
            uop_##func(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_temp0_Q);                    \
        }                                                                                          \
                                                                                                   \
        codegen_mark_code_present(block, cs + op_pc + 1, 1);                                       \
        return op_pc + 2;                                                                          \
    }

// clang-format off
ropParith(PFADD, 0x9e)
ropParith(PFCMPEQ, 0xb0)
ropParith(PFCMPGE, 0x90)
ropParith(PFCMPGT, 0xa0)
ropParith(PFMAX, 0xa4)
ropParith(PFMIN, 0x94)
ropParith(PFMUL, 0xb4)
ropParith(PFSUB, 0x9a)
ropParith(PMULHRW, 0xb7)
ropParith(PAVGUSB, 0xbf)
    // clang-format on

uint32_t ropPF2ID(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0x1d);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PF2ID(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PF2ID(ir, IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPF2IW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0x1c);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PF2IW(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PF2IW(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFACC(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0xae);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PFACC(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFACC(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFNACC(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0x8a);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    /* Snapshot dst before writeback so backend lowering stays alias-safe for ModRM reg overlap. */
    uop_MOV(ir, IREG_temp1_Q, IREG_MM(dest_reg));
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg      = fetchdat & 7;
        int src_reg_ireg = (src_reg == dest_reg) ? IREG_temp1_Q : IREG_MM(src_reg);
        uop_PFNACC(ir, IREG_MM(dest_reg), IREG_temp1_Q, src_reg_ireg);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFNACC(ir, IREG_MM(dest_reg), IREG_temp1_Q, IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFPNACC(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0x8e);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    /* Snapshot dst before writeback so backend lowering stays alias-safe for ModRM reg overlap. */
    uop_MOV(ir, IREG_temp1_Q, IREG_MM(dest_reg));
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg      = fetchdat & 7;
        int src_reg_ireg = (src_reg == dest_reg) ? IREG_temp1_Q : IREG_MM(src_reg);
        uop_PFPNACC(ir, IREG_MM(dest_reg), IREG_temp1_Q, src_reg_ireg);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFPNACC(ir, IREG_MM(dest_reg), IREG_temp1_Q, IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFSUBR(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0xaa);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PFSUB(ir, IREG_MM(dest_reg), IREG_MM(src_reg), IREG_MM(dest_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFSUB(ir, IREG_MM(dest_reg), IREG_temp0_Q, IREG_MM(dest_reg));
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPI2FD(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0x0d);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PI2FD(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PI2FD(ir, IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPI2FW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0x0c);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PI2FW(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PI2FW(ir, IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPSWAPD(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0xbb);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PSWAPD(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PSWAPD(ir, IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFRCPIT(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0xa6);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        if (src_reg != dest_reg)
            uop_MOV(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_MM(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
ropPFRCP(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0x96);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PFRCP(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFRCP(ir, IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}
uint32_t
ropPFRSQRT(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0x97);
    int dest_reg = (fetchdat >> 3) & 7;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_PFRSQRT(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_PFRSQRT(ir, IREG_MM(dest_reg), IREG_temp0_Q);
    }

    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPFRSQIT1(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(uint32_t op_32), uint32_t op_pc)
{
    dynarec_3dnow_cov_count_op(0xa7);
    uop_MMX_ENTER(ir);

    codegen_mark_code_present(block, cs + op_pc, 2);
    return op_pc + 2;
}
