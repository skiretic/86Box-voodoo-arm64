# 86Box New Dynarec Instruction Coverage Audit

Date: 2026-03-03
Auditor: cpu-x86ops agent
Branch: cpu-dynarec-improvements

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Methodology](#methodology)
3. [Dispatch Table Architecture](#dispatch-table-architecture)
4. [One-Byte Opcode Map (00-FF)](#one-byte-opcode-map)
5. [Two-Byte Opcode Map (0F xx)](#two-byte-opcode-map)
6. [FPU Opcode Maps (D8-DF)](#fpu-opcode-maps)
7. [3DNow! Opcode Map (0F 0F xx)](#3dnow-opcode-map)
8. [Coverage by Instruction Family](#coverage-by-instruction-family)
9. [High-Impact Coverage Gaps](#high-impact-coverage-gaps)
10. [Quality Notes on Existing Handlers](#quality-notes)
11. [Prioritized Implementation Roadmap](#implementation-roadmap)

---

## Executive Summary

The 86Box new dynarec JIT covers the core integer workload well but has significant
gaps in areas that matter for real-world performance:

| Area | JIT Slots | NULL Slots | Coverage |
|------|-----------|------------|----------|
| One-byte opcodes (recomp_opcodes) | 356 / 512 | 156 / 512 | **69.5%** |
| Two-byte 0F opcodes (recomp_opcodes_0f) | 134 / 512 | 378 / 512 | **26.2%** |
| Two-byte 0F no-MMX (recomp_opcodes_0f_no_mmx) | 70 / 512 | 442 / 512 | **13.7%** |
| FPU D8 table | 512 / 512 | 0 / 512 | **100%** |
| FPU D9 table | 338 / 512 | 174 / 512 | **66.0%** |
| FPU DA table | 386 / 512 | 126 / 512 | **75.4%** |
| FPU DB table | 192 / 512 | 320 / 512 | **37.5%** |
| FPU DC table | 512 / 512 | 0 / 512 | **100%** |
| FPU DD table | 384 / 512 | 128 / 512 | **75.0%** |
| FPU DE table | 450 / 512 | 62 / 512 | **87.9%** |
| FPU DF table | 258 / 512 | 254 / 512 | **50.4%** |
| 3DNow! (ARM64) | 0 / 256 | 256 / 256 | **0%** |
| 3DNow! (x86-64) | 18 / 256 | 238 / 256 | **7.0%** |

**Critical gaps** (frequently executed, no JIT):
- REP string operations (REP MOVSB/W/D, REP STOSB/W/D, etc.) -- **0% JIT, entire REP prefix forces interpreter**
- MUL/IMUL/DIV/IDIV (8/16/32-bit) -- **0% JIT via ropF6/F7**
- IMUL r,r/m,imm (0F AF, 69, 6B) -- **0% JIT**
- SETcc (0F 90-9F) -- **0% JIT**
- CMOVcc (0F 40-4F) -- **0% JIT**
- BT/BTS/BTR/BTC/BSF/BSR (0F Ax/Bx) -- **0% JIT**
- BSWAP (0F C8-CF) -- **0% JIT**
- CMPXCHG (0F B0/B1) -- **0% JIT**
- POPF/POPFD (9D) -- **0% JIT**
- SAHF/LAHF (9E/9F) -- **0% JIT**
- 3DNow! on ARM64 -- **0% JIT** (entire table is zeroed)

---

## Methodology

Analyzed dispatch tables in `src/codegen_new/codegen_ops.c`:
- `recomp_opcodes[512]` -- primary 1-byte opcodes (entries 0-255 = 16-bit, 256-511 = 32-bit)
- `recomp_opcodes_0f[512]` -- 0F-prefixed 2-byte opcodes (same 16/32 split)
- `recomp_opcodes_0f_no_mmx[512]` -- 0F table for non-MMX CPUs
- `recomp_opcodes_3DNOW[256]` -- 3DNow! (0F 0F xx)
- `recomp_opcodes_d8[512]` through `recomp_opcodes_df[512]` -- FPU escape opcodes

NULL entries mean the instruction falls through to the interpreter (the dynarec emits a
`CALL_FUNC` to the interpreter handler, ending the JIT block for that instruction).

Group opcodes (80/81/83, C0/C1/D0-D3, F6/F7, FE/FF) were examined in their handler
functions to determine which ModRM /reg sub-opcodes are JIT-compiled vs return 0
(interpreter fallback).

---

## Dispatch Table Architecture

The tables are indexed as `[opcode + (op_32 ? 256 : 0)]` where op_32 indicates the
current operand size. Most entries are duplicated for 16-bit and 32-bit mode with
appropriate size variants (e.g., ropADD_w_rmw vs ropADD_l_rmw).

When `recomp_op_table` is set to NULL (as for REP/REPNE prefixes, or fpu_softfloat
mode), the ENTIRE subsequent instruction is interpreter-only.

---

## One-Byte Opcode Map

Legend: [J] = JIT handler, [I] = Interpreter only (NULL), [G] = Group dispatch (partial JIT)

### 16-bit mode (entries 0-255)

```
     x0   x1   x2   x3   x4   x5   x6   x7   x8   x9   xA   xB   xC   xD   xE   xF
0x:  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [I]
1x:  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [I]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]
2x:  [J]  [J]  [J]  [J]  [J]  [J]  [I]  [I]  [J]  [J]  [J]  [J]  [J]  [J]  [I]  [I]
3x:  [J]  [J]  [J]  [J]  [J]  [J]  [I]  [I]  [J]  [J]  [J]  [J]  [J]  [J]  [I]  [I]
4x:  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]
5x:  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]
6x:  [J]  [J]  [I]  [I]  [I]  [I]  [I]  [I]  [J]  [I]  [J]  [I]  [I]  [I]  [I]  [I]
7x:  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]
8x:  [G]  [G]  [G]  [G]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]
9x:  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [I]  [I]  [J]  [I]  [I]  [I]
Ax:  [J]  [J]  [J]  [J]  [I]  [I]  [I]  [I]  [J]  [J]  [I]  [I]  [I]  [I]  [I]  [I]
Bx:  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]
Cx:  [G]  [G]  [J]  [J]  [J]  [J]  [J]  [J]  [I]  [J]  [J]  [J]  [I]  [I]  [I]  [I]
Dx:  [G]  [G]  [G]  [G]  [I]  [I]  [I]  [J]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]
Ex:  [J]  [J]  [J]  [J]  [I]  [I]  [I]  [I]  [J]  [J]  [J]  [J]  [I]  [I]  [I]  [I]
Fx:  [I]  [I]  [I]  [I]  [I]  [J]  [G]  [G]  [J]  [J]  [J]  [J]  [J]  [J]  [G]  [G]
```

### Opcode-by-opcode breakdown (unique opcodes, both 16+32 modes combined)

| Opcode | Instruction | 16-bit | 32-bit | Status |
|--------|-------------|--------|--------|--------|
| 00-05 | ADD b/w/l variants | J | J | Full coverage |
| 06 | PUSH ES | J | J | Full coverage |
| 07 | POP ES | J | J | Full coverage |
| 08-0D | OR b/w/l variants | J | J | Full coverage |
| 0E | PUSH CS | J | J | Full coverage |
| 0F | 2-byte escape | - | - | See 0F table |
| 10-15 | ADC b/w/l variants | J | J | Full coverage |
| 16 | PUSH SS | J | J | Full coverage |
| 17 | POP SS | **I** | **I** | **MISSING** |
| 18-1D | SBB b/w/l variants | J | J | Full coverage |
| 1E | PUSH DS | J | J | Full coverage |
| 1F | POP DS | J | J | Full coverage |
| 20-25 | AND b/w/l variants | J | J | Full coverage |
| 26 | ES: prefix | **I** | **I** | Handled as prefix in loop |
| 27 | DAA | **I** | **I** | **MISSING - BCD** |
| 28-2D | SUB b/w/l variants | J | J | Full coverage |
| 2E | CS: prefix | **I** | **I** | Handled as prefix in loop |
| 2F | DAS | **I** | **I** | **MISSING - BCD** |
| 30-35 | XOR b/w/l variants | J | J | Full coverage |
| 36 | SS: prefix | **I** | **I** | Handled as prefix in loop |
| 37 | AAA | **I** | **I** | **MISSING - BCD** |
| 38-3D | CMP b/w/l variants | J | J | Full coverage |
| 3E | DS: prefix | **I** | **I** | Handled as prefix in loop |
| 3F | AAS | **I** | **I** | **MISSING - BCD** |
| 40-47 | INC r16/r32 | J | J | Full coverage |
| 48-4F | DEC r16/r32 | J | J | Full coverage |
| 50-57 | PUSH r16/r32 | J | J | Full coverage |
| 58-5F | POP r16/r32 | J | J | Full coverage |
| 60 | PUSHA/PUSHAD | J | J | Full coverage |
| 61 | POPA/POPAD | J | J | Full coverage |
| 62 | BOUND | **I** | **I** | **MISSING** (rare) |
| 63 | ARPL | **I** | **I** | **MISSING** (PM only) |
| 64 | FS: prefix | **I** | **I** | Handled as prefix in loop |
| 65 | GS: prefix | **I** | **I** | Handled as prefix in loop |
| 66 | Op size prefix | **I** | **I** | Handled as prefix in loop |
| 67 | Addr size prefix | **I** | **I** | Handled as prefix in loop |
| 68 | PUSH imm16/32 | J | J | Full coverage |
| 69 | IMUL r,r/m,imm16/32 | **I** | **I** | **MISSING - HIGH IMPACT** |
| 6A | PUSH imm8 | J | J | Full coverage |
| 6B | IMUL r,r/m,imm8 | **I** | **I** | **MISSING - HIGH IMPACT** |
| 6C | INS byte | **I** | **I** | MISSING (I/O, rare) |
| 6D | INS word/dword | **I** | **I** | MISSING (I/O, rare) |
| 6E | OUTS byte | **I** | **I** | MISSING (I/O, rare) |
| 6F | OUTS word/dword | **I** | **I** | MISSING (I/O, rare) |
| 70-7F | Jcc rel8 | J | J | Full coverage (all 16 conditions) |
| 80 | Group 1 Eb,Ib | G | G | All 8 sub-ops JIT |
| 81 | Group 1 Ev,Iv | G | G | All 8 sub-ops JIT |
| 82 | Group 1 Eb,Ib (alias) | G | G | Same as 80 |
| 83 | Group 1 Ev,Ib | G | G | All 8 sub-ops JIT |
| 84 | TEST b r/m | J | J | Full coverage |
| 85 | TEST w/l r/m | J | J | Full coverage |
| 86 | XCHG b | J | J | Full coverage |
| 87 | XCHG w/l | J | J | Full coverage |
| 88 | MOV Eb,Gb | J | J | Full coverage |
| 89 | MOV Ev,Gv | J | J | Full coverage |
| 8A | MOV Gb,Eb | J | J | Full coverage |
| 8B | MOV Gv,Ev | J | J | Full coverage |
| 8C | MOV Ev,Sreg | J | J | Full coverage |
| 8D | LEA Gv,M | J | J | Full coverage |
| 8E | MOV Sreg,Ew | J | J | Full coverage |
| 8F | POP Ev | J | J | Full coverage |
| 90 | NOP / XCHG AX,AX | J | J | Full coverage |
| 91-97 | XCHG AX/EAX,r | J | J | Full coverage |
| 98 | CBW / CWDE | J | J | Full coverage |
| 99 | CWD / CDQ | J | J | Full coverage |
| 9A | CALL far imm | **I** | **I** | **MISSING** (far call) |
| 9B | WAIT/FWAIT | **I** | **I** | MISSING (FPU sync) |
| 9C | PUSHF/PUSHFD | J | J | Full coverage |
| 9D | POPF/POPFD | **I** | **I** | **MISSING - MEDIUM IMPACT** |
| 9E | SAHF | **I** | **I** | **MISSING - MEDIUM IMPACT** |
| 9F | LAHF | **I** | **I** | **MISSING - MEDIUM IMPACT** |
| A0-A3 | MOV AL/AX/EAX,moffs | J | J | Full coverage |
| A4 | MOVSB | **I** | **I** | **MISSING - HIGH IMPACT (with REP)** |
| A5 | MOVSW/MOVSD | **I** | **I** | **MISSING - HIGH IMPACT (with REP)** |
| A6 | CMPSB | **I** | **I** | **MISSING - MEDIUM IMPACT** |
| A7 | CMPSW/CMPSD | **I** | **I** | **MISSING - MEDIUM IMPACT** |
| A8-A9 | TEST AL/AX/EAX,imm | J | J | Full coverage |
| AA | STOSB | **I** | **I** | **MISSING - HIGH IMPACT (with REP)** |
| AB | STOSW/STOSD | **I** | **I** | **MISSING - HIGH IMPACT (with REP)** |
| AC | LODSB | **I** | **I** | **MISSING - MEDIUM IMPACT** |
| AD | LODSW/LODSD | **I** | **I** | **MISSING - MEDIUM IMPACT** |
| AE | SCASB | **I** | **I** | **MISSING - MEDIUM IMPACT** |
| AF | SCASW/SCASD | **I** | **I** | **MISSING - MEDIUM IMPACT** |
| B0-B7 | MOV rb,imm8 | J | J | Full coverage |
| B8-BF | MOV r16/r32,imm | J | J | Full coverage |
| C0 | Group 2 Eb,Ib (shift) | G | G | ROL,ROR,SHL,SHR,SAR JIT; **RCL,RCR MISSING** |
| C1 | Group 2 Ev,Ib (shift) | G | G | ROL,ROR,SHL,SHR,SAR JIT; **RCL,RCR MISSING** |
| C2 | RET imm16 | J | J | Full coverage |
| C3 | RET | J | J | Full coverage |
| C4 | LES Gv,Mp | J | J | Full coverage |
| C5 | LDS Gv,Mp | J | J | Full coverage |
| C6 | MOV Eb,Ib | J | J | Full coverage |
| C7 | MOV Ev,Iv | J | J | Full coverage |
| C8 | ENTER | **I** | **I** | **MISSING - LOW-MEDIUM IMPACT** |
| C9 | LEAVE | J | J | Full coverage |
| CA | RETF imm16 | J | J | Full coverage |
| CB | RETF | J | J | Full coverage |
| CC | INT 3 | **I** | **I** | MISSING (debug, rare in user code) |
| CD | INT imm8 | **I** | **I** | **MISSING - HIGH IMPACT (DOS)** |
| CE | INTO | **I** | **I** | MISSING (rare) |
| CF | IRET | **I** | **I** | MISSING (privileged) |
| D0 | Group 2 Eb,1 (shift) | G | G | ROL,ROR,SHL,SHR,SAR JIT; **RCL,RCR MISSING** |
| D1 | Group 2 Ev,1 (shift) | G | G | ROL,ROR,SHL,SHR,SAR JIT; **RCL,RCR MISSING** |
| D2 | Group 2 Eb,CL (shift) | G | G | ROL,ROR,SHL,SHR,SAR JIT; **RCL,RCR MISSING** |
| D3 | Group 2 Ev,CL (shift) | G | G | ROL,ROR,SHL,SHR,SAR JIT; **RCL,RCR MISSING** |
| D4 | AAM | **I** | **I** | **MISSING - BCD** |
| D5 | AAD | **I** | **I** | **MISSING - BCD** |
| D6 | SALC (undoc) | **I** | **I** | MISSING (rare) |
| D7 | XLAT | J | J | Full coverage |
| D8-DF | FPU escape | - | - | See FPU tables below |
| E0 | LOOPNE | J | J | Full coverage |
| E1 | LOOPE | J | J | Full coverage |
| E2 | LOOP | J | J | Full coverage |
| E3 | JCXZ/JECXZ | J | J | Full coverage |
| E4 | IN AL,imm8 | **I** | **I** | MISSING (I/O) |
| E5 | IN AX/EAX,imm8 | **I** | **I** | MISSING (I/O) |
| E6 | OUT imm8,AL | **I** | **I** | MISSING (I/O) |
| E7 | OUT imm8,AX/EAX | **I** | **I** | MISSING (I/O) |
| E8 | CALL rel16/32 | J | J | Full coverage |
| E9 | JMP rel16/32 | J | J | Full coverage |
| EA | JMP far imm | J | J | Full coverage |
| EB | JMP rel8 | J | J | Full coverage |
| EC | IN AL,DX | **I** | **I** | MISSING (I/O) |
| ED | IN AX/EAX,DX | **I** | **I** | MISSING (I/O) |
| EE | OUT DX,AL | **I** | **I** | MISSING (I/O) |
| EF | OUT DX,AX/EAX | **I** | **I** | MISSING (I/O) |
| F0 | LOCK prefix | **I** | **I** | Handled as prefix in loop |
| F1 | INT1/ICEBP | **I** | **I** | MISSING (debug) |
| F2 | REPNE prefix | **I** | **I** | **Forces interpreter for next op** |
| F3 | REP/REPE prefix | **I** | **I** | **Forces interpreter for next op** |
| F4 | HLT | **I** | **I** | MISSING (privileged) |
| F5 | CMC | J | J | Full coverage |
| F6 | Group 3 Eb | G | G | TEST,NOT,NEG JIT; **MUL,IMUL,DIV,IDIV MISSING** |
| F7 | Group 3 Ev | G | G | TEST,NOT,NEG JIT; **MUL,IMUL,DIV,IDIV MISSING** |
| F8 | CLC | J | J | Full coverage |
| F9 | STC | J | J | Full coverage |
| FA | CLI | J | J | Full coverage |
| FB | STI | J | J | Full coverage |
| FC | CLD | J | J | Full coverage |
| FD | STD | J | J | Full coverage |
| FE | Group 4 (INC/DEC Eb) | G | G | INC,DEC JIT; other sub-ops N/A |
| FF | Group 5 | G | G | INC,DEC,CALL,JMP,JMP far,PUSH JIT |

---

## Two-Byte Opcode Map (0F xx)

### With MMX support (recomp_opcodes_0f)

```
     x0   x1   x2   x3   x4   x5   x6   x7   x8   x9   xA   xB   xC   xD   xE   xF
0x:  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]
1x:  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]
2x:  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]
3x:  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]
4x:  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]
5x:  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]
6x:  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [I]  [I]  [J]  [J]
7x:  [I]  [G]  [G]  [G]  [J]  [J]  [J]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [J]  [J]
8x:  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]  [J]
9x:  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]
Ax:  [J]  [J]  [I]  [I]  [J]  [I]  [I]  [I]  [J]  [J]  [I]  [I]  [J]  [I]  [I]  [I]
Bx:  [I]  [I]  [J]  [I]  [J]  [J]  [J]  [J]*  [I]  [I]  [I]  [I]  [I]  [I]  [J]  [J]*
Cx:  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]  [I]
Dx:  [I]  [I]  [I]  [I]  [I]  [J]  [I]  [I]  [J]  [J]  [I]  [J]  [J]  [J]  [I]  [J]
Ex:  [I]  [I]  [I]  [I]  [I]  [J]  [I]  [I]  [J]  [J]  [I]  [J]  [J]  [J]  [I]  [J]
Fx:  [I]  [I]  [I]  [I]  [I]  [J]# [I]  [I]  [J]  [J]  [J]  [I]  [J]  [J]  [J]  [I]
```

*Note: [J]* = 32-bit only for MOVZX_32_16 (B7) and MOVSX_32_16 (BF); 16-bit mode has NULL.
#Note: [J]# = PMADDWD on x86-64 only; NULL on ARM64.

### Key 0F opcode analysis

| Opcode | Instruction | Status | Impact |
|--------|-------------|--------|--------|
| 0F 00 | SLDT/STR/LLDT/LTR/VERR/VERW | I | Low (PM management) |
| 0F 01 | SGDT/SIDT/LGDT/LIDT/SMSW/LMSW/INVLPG | I | Low (PM management) |
| 0F 02 | LAR | I | Low |
| 0F 03 | LSL | I | Low |
| 0F 06 | CLTS | I | Low (privileged) |
| 0F 08 | INVD | I | Low (privileged) |
| 0F 09 | WBINVD | I | Low (privileged) |
| 0F 0B | UD2 | I | N/A (undefined) |
| 0F 0F | 3DNow! escape | - | See 3DNow table |
| 0F 20 | MOV CRn,r32 | I | Low (privileged) |
| 0F 21 | MOV DRn,r32 | I | Low (privileged) |
| 0F 22 | MOV r32,CRn | I | Low (privileged) |
| 0F 23 | MOV r32,DRn | I | Low (privileged) |
| 0F 30 | WRMSR | I | Low (privileged) |
| 0F 31 | RDTSC | I | Medium (timing) |
| 0F 32 | RDMSR | I | Low (privileged) |
| 0F 33 | RDPMC | I | Low |
| 0F 40-4F | **CMOVcc** | **I** | **HIGH - P6+ code uses these heavily** |
| 0F 60-6F | MMX pack/cmp/mov | J (most) | Good coverage |
| 0F 70 | PSHUFW | I | Medium (MMX shuffle) |
| 0F 71-73 | PSxxW/D/Q imm | G | Partial - see MMX section |
| 0F 74-76 | PCMPEQx | J | Full coverage |
| 0F 77 | EMMS | **I** | **MEDIUM - required for MMX cleanup** |
| 0F 80-8F | **Jcc rel16/32** | **J** | Full coverage |
| 0F 90-9F | **SETcc** | **I** | **HIGH - extremely common in compiled code** |
| 0F A0-A1 | PUSH/POP FS | J | Full coverage |
| 0F A2 | CPUID | I | Low (rare in hot path) |
| 0F A3 | BT r/m,r | **I** | **MEDIUM** |
| 0F A4 | SHLD r/m,r,imm | J | Full coverage |
| 0F A5 | SHLD r/m,r,CL | **I** | **MEDIUM** |
| 0F A8-A9 | PUSH/POP GS | J | Full coverage |
| 0F AB | BTS r/m,r | **I** | **MEDIUM** |
| 0F AC | SHRD r/m,r,imm | J | Full coverage |
| 0F AD | SHRD r/m,r,CL | **I** | **MEDIUM** |
| 0F AF | IMUL Gv,Ev | **I** | **HIGH - very common multiply** |
| 0F B0 | CMPXCHG Eb,Gb | **I** | **HIGH for MT code** |
| 0F B1 | CMPXCHG Ev,Gv | **I** | **HIGH for MT code** |
| 0F B2 | LSS Gv,Mp | J | Full coverage |
| 0F B3 | BTR r/m,r | **I** | **MEDIUM** |
| 0F B4-B5 | LFS/LGS | J | Full coverage |
| 0F B6 | MOVZX Gv,Eb | J | Full coverage |
| 0F B7 | MOVZX Gv,Ew | J(32) | 32-bit only; 16-bit NULL |
| 0F BA | Group 8 BT/BTS/BTR/BTC imm | **I** | **MEDIUM** |
| 0F BB | BTC r/m,r | **I** | **MEDIUM** |
| 0F BC | BSF | **I** | **MEDIUM** |
| 0F BD | BSR | **I** | **MEDIUM** |
| 0F BE | MOVSX Gv,Eb | J | Full coverage |
| 0F BF | MOVSX Gv,Ew | J(32) | 32-bit only; 16-bit NULL |
| 0F C0 | XADD Eb,Gb | **I** | **MEDIUM** |
| 0F C1 | XADD Ev,Gv | **I** | **MEDIUM** |
| 0F C8-CF | **BSWAP r32** | **I** | **HIGH - endian conversion, multimedia** |

---

## FPU Opcode Maps (D8-DF)

### D8: FADD/FMUL/FCOM/FCOMP/FSUB/FSUBR/FDIV/FDIVR (mem float32 + ST(i))

| Range | Instruction | Status | Notes |
|-------|-------------|--------|-------|
| 00-07 (mem) | FADDs | J | All 3 ModRM ranges |
| 08-0F (mem) | FMULs | J | |
| 10-17 (mem) | FCOMs | J | |
| 18-1F (mem) | FCOMPs | J | |
| 20-27 (mem) | FSUBs | J | |
| 28-2F (mem) | FSUBRs | J | |
| 30-37 (mem) | FDIVs | J | |
| 38-3F (mem) | FDIVRs | J | |
| C0-C7 (reg) | FADD ST,ST(i) | J | |
| C8-CF (reg) | FMUL ST,ST(i) | J | |
| D0-D7 (reg) | FCOM ST(i) | J | |
| D8-DF (reg) | FCOMP ST(i) | J | |
| E0-E7 (reg) | FSUB ST,ST(i) | J | |
| E8-EF (reg) | FSUBR ST,ST(i) | J | |
| F0-F7 (reg) | FDIV ST,ST(i) | J | |
| F8-FF (reg) | FDIVR ST,ST(i) | J | |
| **Coverage** | | **100%** | |

### D9: FLD/FST/FSTP/FLDENV/FLDCW/FSTENV/FSTCW + misc

| Range | Instruction | Status | Notes |
|-------|-------------|--------|-------|
| 00-07 (mem) | FLDs | J | (D9 44 has a known bug note!) |
| 08-0F (mem) | (invalid) | I | |
| 10-17 (mem) | FSTs | J | |
| 18-1F (mem) | FSTPs | J | |
| 20-27 (mem) | FLDENV | **I** | **MISSING** |
| 28-2F (mem) | FLDCW | **I** | **MISSING - MEDIUM IMPACT** |
| 30-37 (mem) | FSTENV | **I** | **MISSING** |
| 38-3F (mem) | FSTCW | J | |
| C0-C7 (reg) | FLD ST(i) | J | |
| C8-CF (reg) | FXCH ST(i) | J | |
| D0 (reg) | FNOP | **I** | **MISSING** |
| D1-D7 (reg) | (reserved) | I | |
| D8-DF (reg) | FSTP ST(i) | J | (aliased FSTP) |
| E0 (reg) | FCHS | J | |
| E1 (reg) | FABS | J | |
| E2-E3 (reg) | (reserved) | I | |
| E4 (reg) | FTST | J | |
| E5 (reg) | FXAM | **I** | **MISSING** |
| E6-E7 (reg) | (reserved) | I | |
| E8 (reg) | FLD1 | J | |
| E9 (reg) | FLDL2T | **I** | **MISSING** |
| EA (reg) | FLDL2E | **I** | **MISSING** |
| EB (reg) | FLDPI | **I** | **MISSING** |
| EC (reg) | FLDLG2 | **I** | **MISSING** |
| ED (reg) | FLDLN2 | **I** | **MISSING** |
| EE (reg) | FLDZ | J | |
| EF (reg) | (reserved) | I | |
| F0 (reg) | F2XM1 | **I** | **MISSING** |
| F1 (reg) | FYL2X | **I** | **MISSING** |
| F2 (reg) | FPTAN | **I** | **MISSING** |
| F3 (reg) | FPATAN | **I** | **MISSING** |
| F4 (reg) | FXTRACT | **I** | **MISSING** |
| F5 (reg) | FPREM1 | **I** | **MISSING** |
| F6 (reg) | FDECSTP | **I** | **MISSING** |
| F7 (reg) | FINCSTP | **I** | **MISSING** |
| F8 (reg) | FPREM | **I** | **MISSING** |
| F9 (reg) | FYL2XP1 | **I** | **MISSING** |
| FA (reg) | FSQRT | J | |
| FB (reg) | FSINCOS | **I** | **MISSING** |
| FC (reg) | FRNDINT | **I** | **MISSING - MEDIUM IMPACT** |
| FD (reg) | FSCALE | **I** | **MISSING** |
| FE (reg) | FSIN | **I** | **MISSING** |
| FF (reg) | FCOS | **I** | **MISSING** |

### DA: FIADD/FIMUL/FICOM/FICOMP/FISUB/FISUBR/FIDIV/FIDIVR (mem int32) + FUCOMPP

| Range | Instruction | Status |
|-------|-------------|--------|
| 00-3F (mem) | FI* int32 ops | J (all 8 groups) |
| C0-CF (reg) | FCMOVB/FCMOVE etc. | **I (P6 conditional FP moves)** |
| D9 (reg) | FUCOMPP | J |
| All other reg | (reserved) | I |
| **Coverage** | | ~77% (mem full, reg partial) |

### DB: FILD/FIST/FISTP (mem int32) + misc

| Range | Instruction | Status |
|-------|-------------|--------|
| 00-07 (mem) | FILDl | J |
| 08-0F (mem) | FISTTP (SSE3) | I |
| 10-17 (mem) | FISTl | J |
| 18-1F (mem) | FISTPl | J |
| 20-27 (mem) | (reserved) | I |
| 28-2F (mem) | FLD ext80 | **I - MISSING** |
| 30-37 (mem) | (reserved) | I |
| 38-3F (mem) | FSTP ext80 | **I - MISSING** |
| C0-C7 (reg) | FCMOVNB etc. | I (P6) |
| E0-E3 (reg) | FENI/FDISI/FCLEX/FINIT | **I - FINIT MEDIUM IMPACT** |
| E8-EF (reg) | FUCOMI | **I - MISSING** |
| F0-F7 (reg) | FCOMI | **I - MISSING** |
| **Coverage** | | ~37% |

### DC: FADD/FMUL/FCOM/FCOMP/FSUB/FSUBR/FDIV/FDIVR (mem float64 + ST(i),ST)

**100% coverage** -- all memory (float64) and register forms implemented.

### DD: FLD/FST/FSTP (mem float64) + FFREE/FUCOM/FUCOMP/FSTSW

| Range | Instruction | Status |
|-------|-------------|--------|
| 00-07 (mem) | FLDd | J |
| 08-0F (mem) | FISTTP (SSE3) | I |
| 10-17 (mem) | FSTd | J |
| 18-1F (mem) | FSTPd | J |
| 20-27 (mem) | FRSTOR | **I** |
| 28-2F (mem) | (reserved) | I |
| 30-37 (mem) | FSAVE | **I** |
| 38-3F (mem) | FSTSW mem | J |
| C0-C7 (reg) | FFREE | J |
| C8-CF (reg) | (reserved) | I |
| D0-D7 (reg) | FST ST(i) | J |
| D8-DF (reg) | FSTP ST(i) | J |
| E0-E7 (reg) | FUCOM | J |
| E8-EF (reg) | FUCOMP | J |
| F0-FF (reg) | (reserved) | I |
| **Coverage** | | ~75% |

### DE: FIADD/FIMUL etc. (mem int16) + FADDP/FMULP/FSUBP/FDIVP

| Range | Instruction | Status |
|-------|-------------|--------|
| 00-3F (mem) | FI* int16 ops | J (all 8 groups) |
| C0-C7 (reg) | FADDP | J |
| C8-CF (reg) | FMULP | J |
| D0-D7 (reg) | (reserved) | I |
| D9 (reg) | FCOMPP | J |
| E0-E7 (reg) | FSUBRP | J |
| E8-EF (reg) | FSUBP | J |
| F0-F7 (reg) | FDIVRP | J |
| F8-FF (reg) | FDIVP | J |
| **Coverage** | | ~88% |

### DF: FILD/FIST/FISTP (mem int16) + FILD/FISTP (mem int64) + FSTSW AX

| Range | Instruction | Status |
|-------|-------------|--------|
| 00-07 (mem) | FILDw | J |
| 08-0F (mem) | FISTTP (SSE3) | I |
| 10-17 (mem) | FISTw | J |
| 18-1F (mem) | FISTPw | J |
| 20-27 (mem) | FBLD (BCD) | I |
| 28-2F (mem) | FILDq | J |
| 30-37 (mem) | FBSTP (BCD) | I |
| 38-3F (mem) | FISTPq | J |
| E0 (reg) | FSTSW AX | J |
| E8-EF (reg) | FUCOMIP (P6) | **I** |
| F0-F7 (reg) | FCOMIP (P6) | **I** |
| **Coverage** | | ~50% |

---

## 3DNow! Opcode Map (0F 0F xx)

### ARM64: **0% JIT coverage** (entire table is `0`)

The `#ifdef __aarch64__` block zeros out the entire 3DNow table.

### x86-64 only:

| Opcode | Instruction | Status |
|--------|-------------|--------|
| 0D | PI2FD | J |
| 1D | PF2ID | J |
| 90 | PFCMPGE | J |
| 94 | PFMIN | J |
| 96 | PFRCP | J |
| 97 | PFRSQRT | J |
| 9A | PFSUB | J |
| 9E | PFADD | J |
| A0 | PFCMPGT | J |
| A4 | PFMAX | J |
| A6 | PFRCPIT1 | J |
| A7 | PFRSQIT1 | J |
| AA | PFSUBR | J |
| B0 | PFCMPEQ | J |
| B4 | PFMUL | J |
| B6 | PFRCPIT2 | J |
| **Total** | | **18/256 = 7.0%** |

Missing on x86-64: PAVGUSB (BF), PMULHRW (B7), PI2FW (0C), PF2IW (1C), PSWAPD (BB)

---

## Coverage by Instruction Family

### Integer ALU

| Sub-family | JIT Opcodes | Interpreter-Only | Coverage |
|------------|-------------|------------------|----------|
| ADD (all forms) | 18 (6 base + 8 grp + 4 imm_acc) | 0 | 100% |
| SUB (all forms) | 18 | 0 | 100% |
| AND (all forms) | 18 | 0 | 100% |
| OR (all forms) | 18 | 0 | 100% |
| XOR (all forms) | 18 | 0 | 100% |
| CMP (all forms) | 18 | 0 | 100% |
| ADC (all forms) | 18 | 0 | 100% |
| SBB (all forms) | 18 | 0 | 100% |
| TEST (all forms) | 10 | 0 | 100% |
| INC reg | 16 | 0 | 100% |
| DEC reg | 16 | 0 | 100% |
| INC/DEC r/m8 | 2 (via FE) | 0 | 100% |
| INC/DEC r/m16/32 | 4 (via FF) | 0 | 100% |
| NEG (all sizes) | 6 | 0 | 100% |
| NOT (all sizes) | 6 | 0 | 100% |
| **MUL (all sizes)** | **0** | **6** | **0%** |
| **IMUL 1-op (all)** | **0** | **6** | **0%** |
| **IMUL 2-op (0F AF)** | **0** | **2** | **0%** |
| **IMUL 3-op (69/6B)** | **0** | **4** | **0%** |
| **DIV (all sizes)** | **0** | **6** | **0%** |
| **IDIV (all sizes)** | **0** | **6** | **0%** |
| CBW/CWDE/CWD/CDQ | 4 | 0 | 100% |
| **FAMILY TOTAL** | **190** | **30** | **86.4%** |

### Shifts and Rotates

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| SHL (imm/1/CL, 8/16/32) | 18 | 0 | 100% |
| SHR (imm/1/CL, 8/16/32) | 18 | 0 | 100% |
| SAR (imm/1/CL, 8/16/32) | 18 | 0 | 100% |
| ROL (imm/1/CL, 8/16/32) | 18 | 0 | 100% |
| ROR (imm/1/CL, 8/16/32) | 18 | 0 | 100% |
| **RCL (imm/1/CL, 8/16/32)** | **0** | **18** | **0%** |
| **RCR (imm/1/CL, 8/16/32)** | **0** | **18** | **0%** |
| SHLD r/m,r,imm | 2 | 0 | 100% |
| **SHLD r/m,r,CL** | **0** | **2** | **0%** |
| SHRD r/m,r,imm | 2 | 0 | 100% |
| **SHRD r/m,r,CL** | **0** | **2** | **0%** |
| **FAMILY TOTAL** | **94** | **40** | **70.1%** |

### Data Movement

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| MOV r/m,r (all sizes) | 6 | 0 | 100% |
| MOV r,r/m (all sizes) | 6 | 0 | 100% |
| MOV r,imm (all sizes) | 24 | 0 | 100% |
| MOV r/m,imm (b/w/l) | 6 | 0 | 100% |
| MOV acc,moffs (all) | 6 | 0 | 100% |
| MOV Sreg (read/write) | 3 | 0 | 100% |
| LEA (16/32) | 2 | 0 | 100% |
| MOVZX (all) | 5 | 0 | 100% (B7 32-bit only) |
| MOVSX (all) | 5 | 0 | 100% (BF 32-bit only) |
| XCHG acc,r | 14 | 0 | 100% |
| XCHG r/m,r (8/16/32) | 3 | 0 | 100% |
| XLAT | 1 | 0 | 100% |
| NOP | 1 | 0 | 100% |
| **BSWAP r32** | **0** | **8** | **0%** |
| **CMOVcc** | **0** | **32** | **0%** |
| **CMPXCHG** | **0** | **4** | **0%** |
| **XADD** | **0** | **4** | **0%** |
| **FAMILY TOTAL** | **82** | **48** | **63.1%** |

### Control Flow

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| Jcc rel8 (all 16) | 16 | 0 | 100% |
| Jcc rel16/32 (all 16) | 32 | 0 | 100% |
| JMP rel8/16/32 | 3 | 0 | 100% |
| JMP far imm | 2 | 0 | 100% |
| JMP r/m (via FF) | 2 | 0 | 100% |
| JMP far r/m (via FF) | 2 | 0 | 100% |
| CALL rel16/32 | 2 | 0 | 100% |
| CALL r/m (via FF) | 2 | 0 | 100% |
| RET / RET imm | 4 | 0 | 100% |
| RETF / RETF imm | 4 | 0 | 100% |
| LOOP/LOOPE/LOOPNE | 3 | 0 | 100% |
| JCXZ/JECXZ | 1 | 0 | 100% |
| **SETcc** | **0** | **32** | **0%** |
| CALL far imm (9A) | 0 | 2 | 0% |
| INT/INT3/INTO/IRET | 0 | 8 | 0% (system) |
| **FAMILY TOTAL** | **73** | **42** | **63.5%** |

### Stack Operations

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| PUSH r16/r32 | 16 | 0 | 100% |
| POP r16/r32 | 16 | 0 | 100% |
| PUSH imm16/32 | 2 | 0 | 100% |
| PUSH imm8 (sign-ext) | 2 | 0 | 100% |
| PUSH/POP Sreg | 18 | 2 | 90% (POP SS missing) |
| PUSHA/POPA | 4 | 0 | 100% |
| POP r/m (8F) | 2 | 0 | 100% |
| PUSH r/m (FF /6) | 2 | 0 | 100% |
| PUSHF/PUSHFD | 2 | 0 | 100% |
| **POPF/POPFD** | **0** | **2** | **0%** |
| LEAVE | 2 | 0 | 100% |
| **ENTER** | **0** | **2** | **0%** |
| **FAMILY TOTAL** | **66** | **6** | **91.7%** |

### String Operations

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| MOVSB/MOVSW/MOVSD | 0 | 6 | 0% |
| STOSB/STOSW/STOSD | 0 | 6 | 0% |
| LODSB/LODSW/LODSD | 0 | 6 | 0% |
| CMPSB/CMPSW/CMPSD | 0 | 6 | 0% |
| SCASB/SCASW/SCASD | 0 | 6 | 0% |
| REP prefix | Forces interpreter | - | - |
| REPNE prefix | Forces interpreter | - | - |
| **FAMILY TOTAL** | **0** | **30** | **0%** |

### BCD Instructions

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| DAA (27) | 0 | 1 | 0% |
| DAS (2F) | 0 | 1 | 0% |
| AAA (37) | 0 | 1 | 0% |
| AAS (3F) | 0 | 1 | 0% |
| AAM (D4) | 0 | 1 | 0% |
| AAD (D5) | 0 | 1 | 0% |
| **FAMILY TOTAL** | **0** | **6** | **0%** |

### Flag Manipulation

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| CLC/STC/CMC | 3 | 0 | 100% |
| CLD/STD | 2 | 0 | 100% |
| CLI/STI | 2 | 0 | 100% |
| **SAHF** | **0** | **1** | **0%** |
| **LAHF** | **0** | **1** | **0%** |
| **FAMILY TOTAL** | **7** | **2** | **77.8%** |

### Segment Operations

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| LDS/LES/LFS/LGS/LSS | 10 | 0 | 100% |
| MOV Sreg | 3 | 0 | 100% |
| **FAMILY TOTAL** | **13** | **0** | **100%** |

### I/O Operations

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| IN/OUT (all forms) | 0 | 12 | 0% |
| INS/OUTS (all forms) | 0 | 8 | 0% |
| **FAMILY TOTAL** | **0** | **20** | **0%** |

*Note: I/O operations inherently need to trap to device emulation. JIT is not
expected to help here.*

### FPU (x87)

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| FADD/FSUB/FMUL/FDIV (all forms) | ~192 | 0 | 100% |
| FCOM/FUCOM (all forms) | ~64 | 0 | 100% |
| FLD/FST/FSTP (float32/64) | ~96 | 0 | 100% |
| FILD/FIST/FISTP (int16/32/64) | ~80 | 0 | 100% |
| FIADD/FISUB/FIMUL/FIDIV (int16/32) | ~192 | 0 | 100% |
| FADDP/FSUBP/FMULP/FDIVP | ~64 | 0 | 100% |
| FLD1/FLDZ | 2 | 0 | 100% |
| **FLDL2T/FLDL2E/FLDPI/FLDLG2/FLDLN2** | **0** | **5** | **0%** |
| FABS/FCHS/FSQRT/FTST | 4 | 0 | 100% |
| FXCH | 1 | 0 | 100% |
| FFREE | 1 | 0 | 100% |
| FSTCW/FSTSW/FSTSW_AX | 3 | 0 | 100% |
| **FLDCW** | **0** | **1** | **0%** |
| **FLDENV/FSTENV/FRSTOR/FSAVE** | **0** | **4** | **0%** |
| **FCLEX/FINIT** | **0** | **2** | **0%** |
| **FRNDINT** | **0** | **1** | **0%** |
| **FXAM** | **0** | **1** | **0%** |
| **FSIN/FCOS/FSINCOS/FPTAN/FPATAN** | **0** | **5** | **0%** |
| **F2XM1/FYL2X/FYL2XP1/FSCALE/FPREM/FPREM1/FXTRACT** | **0** | **7** | **0%** |
| **FDECSTP/FINCSTP** | **0** | **2** | **0%** |
| **FLD ext80 / FSTP ext80** | **0** | **2** | **0%** |
| **FUCOMI/FCOMI/FUCOMIP/FCOMIP (P6)** | **0** | **4** | **0%** |
| **FCMOVcc (P6 FP conditional moves)** | **0** | **6** | **0%** |
| **FAMILY TOTAL** | **~700** | **~40** | **~95%** |

### MMX

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| MOVD/MOVQ | 4 | 0 | 100% |
| PADDB/W/D | 6 | 0 | 100% |
| PSUBB/W/D | 6 | 0 | 100% |
| PADDSB/SW | 4 | 0 | 100% |
| PSUBSB/SW | 4 | 0 | 100% |
| PADDUSB/USW | 4 | 0 | 100% |
| PSUBUSB/USW | 4 | 0 | 100% |
| PMULLW | 2 | 0 | 100% |
| PMULHW | 2 | 0 | 100% |
| PMADDWD | 0 (ARM64), 2 (x64) | 2 (ARM64) | 50-100% |
| PAND/PANDN/POR/PXOR | 8 | 0 | 100% |
| PCMPEQB/W/D | 6 | 0 | 100% |
| PCMPGTB/W/D | 6 | 0 | 100% |
| PACKSSWB/DW | 4 | 0 | 100% |
| PACKUSWB | 2 | 0 | 100% |
| PUNPCKL/H BW/WD/DQ | 12 | 0 | 100% |
| PSxxW/D/Q imm | 6 (group) | varies | Partial |
| **PSLLW/D/Q reg** | **0** | **6** | **0%** |
| **PSRLW/D/Q reg** | **0** | **6** | **0%** |
| **PSRAW/D reg** | **0** | **4** | **0%** |
| **EMMS** | **0** | **2** | **0%** |
| **PSHUFW** | **0** | **2** | **0%** |
| **FAMILY TOTAL** | **~80** | **~20** | **~80%** |

### SSE/SSE2

**0% JIT coverage.** No SSE or SSE2 dispatch entries exist in any table.
All SSE/SSE2 instructions fall through to the interpreter.

### System/Privileged

| Sub-family | JIT | Interpreter | Coverage |
|------------|-----|-------------|----------|
| MOV CRn/DRn | 0 | 8 | 0% |
| LGDT/LIDT/SGDT/SIDT | 0 | 4 | 0% |
| LLDT/LTR/SLDT/STR | 0 | 4 | 0% |
| LMSW/SMSW | 0 | 2 | 0% |
| WRMSR/RDMSR/RDTSC | 0 | 3 | 0% |
| CPUID | 0 | 1 | 0% |
| INVLPG/INVD/WBINVD | 0 | 3 | 0% |
| HLT | 0 | 1 | 0% |
| **FAMILY TOTAL** | **0** | **26** | **0%** |

*Note: System/privileged instructions are rare in user-mode hot paths and
generally trigger mode switches. Not a priority for JIT.*

---

## High-Impact Coverage Gaps

Ranked by estimated frequency in real workloads:

### Tier 1: Critical (DOS/Win9x/WinXP daily execution, often in hot loops)

| # | Instruction | Opcodes | Impact | Difficulty |
|---|-------------|---------|--------|------------|
| 1 | **REP MOVSB/W/D** | F3 A4/A5 | Block copies everywhere | Hard (loop UOP or call) |
| 2 | **REP STOSB/W/D** | F3 AA/AB | memset everywhere | Hard (loop UOP or call) |
| 3 | **IMUL r,r/m** | 0F AF | Most common multiply form in 32-bit code | Medium |
| 4 | **IMUL r,r/m,imm** | 69/6B | Array index scaling, very common | Medium |
| 5 | **SETcc** | 0F 90-9F | Branchless conditionals, compiler output | Easy |
| 6 | **MUL/IMUL 1-op** | F6/F7 /4, /5 | 8/16/32-bit multiply | Medium-Hard |
| 7 | **DIV/IDIV** | F6/F7 /6, /7 | Division (modulo, formatting) | Hard |
| 8 | **CMOVcc** | 0F 40-4F | P6+ branchless code, very common | Medium |
| 9 | **BSWAP r32** | 0F C8-CF | Endian swap, networking, multimedia | Easy |
| 10 | **POPF/POPFD** | 9D | Function epilogues, ISR | Medium |

### Tier 2: High (common in compiled code and libraries)

| # | Instruction | Opcodes | Impact | Difficulty |
|---|-------------|---------|--------|------------|
| 11 | **SAHF** | 9E | FPU comparison pattern (FSTSW AX; SAHF; Jcc) | Easy-Medium |
| 12 | **LAHF** | 9F | Flag preservation | Easy |
| 13 | **BT r/m,r** | 0F A3 | Bit testing, bitmask checks | Medium |
| 14 | **BSF/BSR** | 0F BC/BD | Find first/last set bit | Medium |
| 15 | **SHLD/SHRD CL** | 0F A5/AD | Variable double-precision shifts | Medium |
| 16 | **CMPXCHG** | 0F B0/B1 | Atomics, sync primitives | Medium-Hard |
| 17 | **BTS/BTR/BTC** | 0F AB/B3/BB | Bit manipulation | Medium |
| 18 | **XADD** | 0F C0/C1 | Atomic increment | Medium |
| 19 | **FLDCW** | D9 /5 | FPU control word changes (rounding mode) | Easy |
| 20 | **EMMS** | 0F 77 | MMX cleanup | Easy |

### Tier 3: Medium (FPU transcendentals, P6 features)

| # | Instruction | Opcodes | Impact | Difficulty |
|---|-------------|---------|--------|------------|
| 21 | FRNDINT | D9 FC | FPU rounding | Easy |
| 22 | FLDL2T/FLDL2E/FLDPI | D9 E9-ED | FPU constants | Very Easy |
| 23 | FSIN/FCOS/FSINCOS | D9 FE/FF/FB | Trig functions | Medium (call helper) |
| 24 | FPATAN/FPTAN | D9 F3/F2 | Trig functions | Medium |
| 25 | FUCOMI/FCOMI | DB E8/F0 | P6 FP comparison (sets EFLAGS directly) | Medium |
| 26 | FUCOMIP/FCOMIP | DF E8/F0 | P6 FP comparison + pop | Medium |
| 27 | INT imm8 | CD | DOS software interrupts | Medium |
| 28 | ENTER | C8 | Stack frame setup (some compilers) | Medium |
| 29 | REP CMPSB/W/D | F2/F3 A6/A7 | String comparison, memcmp | Hard |
| 30 | REP SCASB/W/D | F2/F3 AE/AF | String search, strlen | Hard |

### Tier 4: Low-Priority

- BCD (DAA, DAS, AAA, AAS, AAM, AAD) -- DOS only, very rare
- 3DNow! on ARM64 -- only matters for AMD K6-2/K6-III guest CPUs
- SSE/SSE2 -- would need new UOP types and massive backend work
- System instructions (LGDT, LIDT, etc.) -- OS kernel only
- I/O (IN/OUT) -- inherently need device emulation traps
- LOCK prefix -- needs memory ordering semantics
- FCMOVcc -- rare (P6+ only, most compilers avoid)
- FLDENV/FSTENV/FRSTOR/FSAVE -- context switch only

---

## Quality Notes on Existing Handlers

### Observations

1. **ropF6/F7 early exit for MUL/DIV**: The check `if (fetchdat & 0x20) return 0`
   cleanly rejects sub-opcodes 4-7 (MUL/IMUL/DIV/IDIV). This means these very
   common operations ALWAYS fall to interpreter even when the rest of the group
   (TEST/NOT/NEG) is JIT-compiled.

2. **D9 44 bug note**: There is an explicit comment in the D9 table:
   `/* TODO: Fix the recompilation of D9 44 so Blood II's gameplay music no longer breaks! */`
   The entry at D9 offset 0x44 is NULL (FLDs with ModRM 44h = SIB addressing with
   disp8). This is a known correctness issue.

3. **PMADDWD missing on ARM64**: The `#ifdef` at 0F F5 zeros it out on ARM64,
   reducing MMX coverage on the primary target platform.

4. **ADC/SBB UOP count**: Each ADC instruction emits 7 UOPs (get_cf, MOVZX, ADD_IMM,
   MOV_IMM, ADD, MOV_IMM, MOVZX). This is somewhat heavy but appears necessary
   for correct flag tracking with the carry-in.

5. **Shift handlers call flags_rebuild**: ROL/ROR handlers call `flags_rebuild()`
   before the shift to snapshot the current flags state. This is correct (OF/CF are
   the only flags affected) but forces a full flag materialization that might not
   always be needed.

6. **rebuild_c in INC/DEC handlers**: The INC/DEC handlers in ropFF and ropINCDEC
   correctly call `rebuild_c()` to preserve the carry flag (INC/DEC don't affect CF).
   The rebuild_c function is smart enough to skip if the last op was already INC/DEC.

7. **No flag accumulation in group dispatchers**: The group opcodes (80-83) set
   flags for every sub-operation. The IR optimizer should be able to eliminate
   redundant flag computations when multiple ALU ops are chained, but the individual
   handlers don't attempt this.

8. **FPU coverage is strong for arithmetic**: D8/DC (float32/float64 arithmetic +
   register forms) are 100% covered. The main gaps are in transcendentals, constants,
   and control/status operations.

9. **3DNow! zeroed on ARM64**: This is an explicit platform decision. Since 3DNow!
   requires SSE-style float operations on 64-bit vectors, the ARM64 NEON backend
   could support this but nobody has implemented it.

---

## Implementation Roadmap

### Phase 1: Quick Wins (Easy, High Impact)

Estimated effort: 1-2 days each

1. **SETcc** (0F 90-9F) -- 16 conditions, all use same pattern: test condition,
   store 0 or 1 to byte operand. Reuse Jcc condition evaluation.

2. **BSWAP r32** (0F C8-CF) -- Byte-swap a 32-bit register. Single UOP if backend
   supports it, otherwise 4-UOP shift/mask sequence.

3. **EMMS** (0F 77) -- Just stores a tag word value. Trivial.

4. **LAHF/SAHF** (9E/9F) -- Load/store AH from/to flags byte. SAHF is part of the
   classic FSTSW AX / SAHF / Jcc pattern used before FCOMI existed.

5. **FPU constants** (D9 E9-EE) -- FLDL2T, FLDL2E, FLDPI, FLDLG2, FLDLN2.
   Just load constant doubles. Copy the FLD1/FLDZ pattern.

6. **FLDCW** (D9 /5) -- Load FPU control word from memory. Common before
   floating-point operations that need specific rounding modes.

### Phase 2: Core Multiply/Divide (Medium, Critical Impact)

Estimated effort: 3-5 days total

7. **IMUL r,r/m** (0F AF) -- Two-operand multiply. Backend needs MUL UOP support.
   Only lower half of result needed, flags partially defined.

8. **IMUL r,r/m,imm** (69/6B) -- Three-operand multiply. Same as above but with
   immediate multiplicand.

9. **MUL/IMUL 1-operand** (F6/F7 /4,/5) -- Full-width multiply producing double-size
   result (AX=AL*r/m8, DX:AX=AX*r/m16, EDX:EAX=EAX*r/m32). Needs UOP for widening
   multiply or helper function call.

10. **DIV/IDIV** (F6/F7 /6,/7) -- Division with remainder. Complex: can fault on
    divide-by-zero and overflow. Best implemented as CALL_FUNC to helper.

### Phase 3: Conditional Moves and Bit Operations (Medium, High Impact)

Estimated effort: 3-5 days total

11. **CMOVcc** (0F 40-4F) -- Conditional move. 16 conditions x 2 sizes. Needs a
    conditional move UOP or branch-around pattern.

12. **BT/BTS/BTR/BTC** (0F A3/AB/B3/BB + 0F BA group) -- Bit test/modify. Test a
    bit in r/m, set CF accordingly. BTS/BTR/BTC also set/reset/complement.

13. **BSF/BSR** (0F BC/BD) -- Bit scan. Find first/last set bit. Backend can use
    native CLZ/CTZ instructions.

### Phase 4: String Operations (Hard, Critical Impact)

Estimated effort: 5-10 days total

14. **REP MOVSB/W/D** -- Block memory copy. Most impactful single improvement.
    Options: (a) inline loop with branch, (b) call optimized helper, (c) special
    block-copy UOP.

15. **REP STOSB/W/D** -- Block memory fill. Same approach as REP MOVS.

16. **REP CMPSB/W/D and REP SCASB/W/D** -- Less common but used for string
    operations and memcmp patterns.

### Phase 5: Remaining Gaps

17. **POPF/POPFD** -- Pop flags. Needs careful handling (mode bits, etc.)
18. **RCL/RCR** -- Rotate through carry. Needs carry flag input.
19. **SHLD/SHRD CL** -- Variable-count double shifts.
20. **CMPXCHG/XADD** -- Atomic operations.
21. **ENTER** -- Stack frame setup.
22. **FPU transcendentals** -- FSIN, FCOS, etc. Call C library functions.
23. **FUCOMI/FCOMI** -- P6 FP comparison to EFLAGS.
24. **3DNow! on ARM64** -- Port x86-64 implementations to ARM64 NEON.

---

## Appendix: Complete Null Entry Count

### recomp_opcodes[512] -- 1-byte opcodes

**16-bit half (0-255):**
NULL entries at: 0F(=prefix), 17, 26, 27, 2E, 2F, 36, 37, 3E, 3F, 62, 63,
64, 65, 66, 67, 69, 6B, 6C, 6D, 6E, 6F, 9A, 9B, 9D, 9E, 9F, A4, A5, A6,
A7, AA, AB, AC, AD, AE, AF, C8, CC, CD, CE, CF, D4, D5, D6, D8-DF(=FPU escape),
E4, E5, E6, E7, EC, ED, EE, EF, F0, F1, F2, F3, F4

Total NULL (16-bit): 78 entries
Total JIT (16-bit): 178 entries

**32-bit half (256-511):**
Same pattern. Total NULL: 78, Total JIT: 178

**Grand total: 356 JIT / 156 NULL = 69.5% coverage**

Note: Several NULL entries are prefix bytes (26, 2E, 36, 3E, 64, 65, 66, 67,
F0, F2, F3) which are handled in the prefix loop before table lookup. Others
are FPU escapes (D8-DF) handled by separate tables. Removing these:
- 11 prefix bytes x 2 = 22
- 8 FPU escapes x 2 = 16
- Effective NULL (excluding prefixes/escapes): 156 - 22 - 16 = **118 true gaps**
- Effective JIT: 356 / (512 - 38) = 356 / 474 = **75.1% effective coverage**

### recomp_opcodes_0f[512] -- 2-byte opcodes

**16-bit half:** 67 JIT entries, 189 NULL
**32-bit half:** 67 JIT entries, 189 NULL
**Grand total: 134 JIT / 378 NULL = 26.2% coverage**

Most of 00-5F and 90-FF rows are empty. The filled areas are:
- 60-6F, 71-76, 7E-7F: MMX (24 JIT)
- 80-8F: Jcc rel16/32 (16 JIT)
- A0-A1, A4, A8-A9, AC: PUSH/POP FS/GS + SHLD/SHRD (8 JIT)
- B2, B4-B7, BE-BF: LSS/LFS/LGS/MOVZX/MOVSX (8 JIT)
- D5, D8-D9, DB-DF, E5, E8-E9, EB-EF, F5, F8-FA, FC-FE: MMX arith/logic (22 JIT)

### FPU tables summary

| Table | JIT entries | NULL entries |
|-------|-------------|--------------|
| D8 | 512 | 0 |
| D9 | 338 | 174 |
| DA | 386 | 126 |
| DB | 192 | 320 |
| DC | 512 | 0 |
| DD | 384 | 128 |
| DE | 450 | 62 |
| DF | 258 | 254 |
| **Total** | **3032** | **1064** |
| **FPU Coverage** | | **74.0%** |

---

*End of audit. Generated by cpu-x86ops agent.*
