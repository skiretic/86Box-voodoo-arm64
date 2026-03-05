/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Bit test and bit scan JIT handler declarations.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */

/* BT r/m16, r16 (0F A3) */
uint32_t ropBT_w(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* BT r/m32, r32 (0F A3) */
uint32_t ropBT_l(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* BTS r/m16, r16 (0F AB) */
uint32_t ropBTS_w(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* BTS r/m32, r32 (0F AB) */
uint32_t ropBTS_l(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* BTR r/m16, r16 (0F B3) */
uint32_t ropBTR_w(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* BTR r/m32, r32 (0F B3) */
uint32_t ropBTR_l(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* BTC r/m16, r16 (0F BB) */
uint32_t ropBTC_w(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* BTC r/m32, r32 (0F BB) */
uint32_t ropBTC_l(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* BSF r16, r/m16 (0F BC) */
uint32_t ropBSF_w(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* BSF r32, r/m32 (0F BC) */
uint32_t ropBSF_l(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* BSR r16, r/m16 (0F BD) */
uint32_t ropBSR_w(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

/* BSR r32, r/m32 (0F BD) */
uint32_t ropBSR_l(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
