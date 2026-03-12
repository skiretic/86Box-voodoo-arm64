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
uint32_t ropREP_MOVSB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREP_MOVSW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREP_MOVSL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREP_STOSB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREP_STOSW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREP_STOSL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREPE_CMPSB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREPNE_CMPSB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREPE_CMPSW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREPE_CMPSL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREPNE_CMPSW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREPNE_CMPSL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREPE_SCASB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropREPNE_SCASB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropDEBUG_REPE_SCASB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropDEBUG_REPE_SCASW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropDEBUG_REPE_SCASL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropDEBUG_REPNE_SCASB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropDEBUG_REPNE_SCASW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropDEBUG_REPNE_SCASL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

uint32_t ropCMPSB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropCMPSW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropCMPSL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropSCASB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropSCASW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropSCASL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

uint32_t ropLODSB(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropLODSW(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);
uint32_t ropLODSL(codeblock_t *block, struct ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc);

#endif
