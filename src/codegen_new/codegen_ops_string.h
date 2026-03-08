#ifndef _CODEGEN_OPS_STRING_H_
#define _CODEGEN_OPS_STRING_H_

#include "codegen.h"

struct ir_data_t;

uint32_t ropSTOSB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropSTOSW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropSTOSL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

uint32_t ropMOVSB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropMOVSW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropMOVSL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

uint32_t ropLODSB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropLODSW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropLODSL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

#endif
