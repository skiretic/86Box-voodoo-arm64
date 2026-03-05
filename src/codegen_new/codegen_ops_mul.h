/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Multiply/divide JIT handler declarations.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */

/* IMUL r16, r/m16 (0F AF) */
uint32_t ropIMUL_w_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* IMUL r32, r/m32 (0F AF) */
uint32_t ropIMUL_l_rm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* IMUL r16, r/m16, imm16 (69) */
uint32_t ropIMUL_w_rm_iw(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* IMUL r32, r/m32, imm32 (69) */
uint32_t ropIMUL_l_rm_il(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* IMUL r16, r/m16, imm8 sign-extended (6B) */
uint32_t ropIMUL_w_rm_ib(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* IMUL r32, r/m32, imm8 sign-extended (6B) */
uint32_t ropIMUL_l_rm_ib(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
