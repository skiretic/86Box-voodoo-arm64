# UOP Catalog

Source: `src/codegen_new/codegen_ir_defs.h`

## Summary

| Category | Count | Description |
|----------|-------|-------------|
| Function Call / Args | 11 | Load function arguments, call functions, instruction fallback |
| Store to Pointer | 3 | Write immediate to memory-mapped pointer |
| Segment / Jump | 4 | Load segment, unconditional jump, jump dest, NOP barrier |
| Data Movement | 13 | MOV, MOVZX, MOVSX, register-pointer loads, FP/int conversions |
| Integer ALU | 12 | ADD, SUB, AND, OR, XOR, ANDN, shifts, rotates |
| Memory Load/Store | 12 | Emulated memory access (with segment + address) |
| Compare / Branch (exit) | 4 | CMP + conditional exit (leave block) |
| Compare / Branch (internal) | 14 | CMP + conditional jump within block |
| Test / Branch (internal) | 2 | TEST sign + conditional jump within block |
| FPU | 11 | x87 FP arithmetic, compare, abs, chs, sqrt, enter |
| MMX Arithmetic | 17 | Packed add, sub (byte/word/long, with/without saturation) |
| MMX Shift | 9 | Packed shift left/right arithmetic/logical (word/long/quad) |
| MMX Compare | 6 | Packed compare equal/greater-than (byte/word/long) |
| MMX Pack/Unpack | 9 | Pack, unpack high/low (byte/word/long) |
| MMX Multiply | 3 | Packed multiply low/high/add-accumulate |
| 3DNow! Float | 14 | Packed float arithmetic, compare, convert, reciprocal |
| Debug | 1 | Log instruction (DEBUG_EXTRA only) |
| **TOTAL** | **145** | |

## Type Flag Legend

| Abbreviation | Meaning |
|---|---|
| REGS | Uses src/dest registers (`UOP_TYPE_PARAMS_REGS`) |
| IMM | Uses immediate data (`UOP_TYPE_PARAMS_IMM`) |
| PTR | Uses pointer field (`UOP_TYPE_PARAMS_POINTER`) |
| BARRIER | Full barrier -- all registers written back/discarded |
| ORDER | Order barrier -- registers written back but preserved |
| JUMP | Branch instruction, target in `jump_dest_uop` |
| JUMP_DEST | Branch target marker |

## Register Operand Convention

All UOPs use up to 4 registers from the `uop_t` struct:
- `dest_reg_a` -- destination register (written)
- `src_reg_a` -- first source register (read)
- `src_reg_b` -- second source register (read)
- `src_reg_c` -- third source register (read)

Plus optional `imm_data` (32-bit on x86-64, pointer-width on ARM) and `void *p`.

---

## 1. Function Call / Argument Loading

These UOPs handle calling external C functions from JIT code. The argument
loaders move values into host calling convention registers before the call.

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 1 | `UOP_LOAD_FUNC_ARG_0` | 0x00 | REGS | src_a -> arg0 | Load src_reg_a into function argument 0 |
| 2 | `UOP_LOAD_FUNC_ARG_1` | 0x01 | REGS | src_a -> arg1 | Load src_reg_a into function argument 1 |
| 3 | `UOP_LOAD_FUNC_ARG_2` | 0x02 | REGS | src_a -> arg2 | Load src_reg_a into function argument 2 |
| 4 | `UOP_LOAD_FUNC_ARG_3` | 0x03 | REGS | src_a -> arg3 | Load src_reg_a into function argument 3 |
| 5 | `UOP_LOAD_FUNC_ARG_0_IMM` | 0x08 | IMM, BARRIER | imm -> arg0 | Load immediate into function argument 0 |
| 6 | `UOP_LOAD_FUNC_ARG_1_IMM` | 0x09 | IMM, BARRIER | imm -> arg1 | Load immediate into function argument 1 |
| 7 | `UOP_LOAD_FUNC_ARG_2_IMM` | 0x0a | IMM, BARRIER | imm -> arg2 | Load immediate into function argument 2 |
| 8 | `UOP_LOAD_FUNC_ARG_3_IMM` | 0x0b | IMM, BARRIER | imm -> arg3 | Load immediate into function argument 3 |
| 9 | `UOP_CALL_FUNC` | 0x10 | PTR, BARRIER | call p() | Call function at pointer p, no return value |
| 10 | `UOP_CALL_INSTRUCTION_FUNC` | 0x11 | PTR, IMM, BARRIER | call p(fetchdat), check result | Call instruction handler, exit block if non-zero return |
| 11 | `UOP_CALL_FUNC_RESULT` | 0x16 | REGS, PTR, BARRIER | dest = call p() | Call function at pointer p, store return value in dest_reg |

**Notes:**
- `UOP_CALL_INSTRUCTION_FUNC` is the interpreter fallback path. When the
  dynarec cannot JIT-compile an instruction, it wraps the interpreter handler
  in this UOP. Reducing calls to this UOP is a primary optimization goal.
- `UOP_LOAD_FUNC_ARG_*_IMM` variants are BARRIER because they write to fixed
  host registers (argument passing convention) which must not be reordered.
- `UOP_LOAD_FUNC_ARG_*` (register variants) are NOT barriers, allowing the
  register allocator to potentially fold them.

---

## 2. Store to Pointer

Direct stores of immediate values to memory-mapped locations (typically
cpu_state fields).

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 12 | `UOP_STORE_P_IMM` | 0x12 | IMM | *p = imm (32-bit) | Store 32-bit immediate to pointer |
| 13 | `UOP_STORE_P_IMM_8` | 0x13 | IMM | *p = imm (8-bit) | Store 8-bit immediate to pointer |
| 14 | `UOP_STORE_P_IMM_16` | 0x19 | IMM | *p = imm (16-bit) | Store 16-bit immediate to pointer |

**Notes:**
- These are used to write constants to cpu_state fields (e.g., setting flags_op
  to indicate the type of flag computation needed for lazy evaluation).
- No register operands -- both address and value are compile-time constants.

---

## 3. Segment Load / Jump / Barrier

Control flow and segment management UOPs.

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 15 | `UOP_LOAD_SEG` | 0x14 | REGS, PTR, BARRIER | loadseg(p, src_a) | Load segment register via loadseg(), exit if fail |
| 16 | `UOP_JMP` | 0x15 | PTR, ORDER | goto p | Unconditional jump to pointer (block exit) |
| 17 | `UOP_JMP_DEST` | 0x17 | IMM, PTR, ORDER, JUMP | goto p (with dest offset) | Jump with destination offset for block linking |
| 18 | `UOP_NOP_BARRIER` | 0x18 | BARRIER | (none) | No-op that forces full register flush |

**Notes:**
- `UOP_JMP` is typically used to jump to `codegen_exit_rout` (the block exit
  routine that returns to the dispatcher).
- `UOP_JMP_DEST` is used for intra-block conditional branches that need their
  target address patched at compile time.
- `UOP_NOP_BARRIER` forces all registers to be written back -- used at points
  where the emulated machine state must be fully consistent.

---

## 4. Data Movement

Register-to-register moves, immediate loads, pointer loads, and type
conversions.

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 19 | `UOP_MOV_PTR` | 0x20 | REGS, PTR | dest = p | Load pointer value into register |
| 20 | `UOP_MOV_IMM` | 0x21 | REGS, IMM | dest = imm | Load immediate into register |
| 21 | `UOP_MOV` | 0x22 | REGS | dest = src_a | Register-to-register move |
| 22 | `UOP_MOVZX` | 0x23 | REGS | dest = zext(src_a) | Zero-extend source to destination size |
| 23 | `UOP_MOVSX` | 0x24 | REGS | dest = sext(src_a) | Sign-extend source to destination size |
| 24 | `UOP_MOV_DOUBLE_INT` | 0x25 | REGS | dest = (double)src_a | Convert integer to double |
| 25 | `UOP_MOV_INT_DOUBLE` | 0x26 | REGS | dest = (int)src_a | Convert double to 32-bit integer (with rounding control) |
| 26 | `UOP_MOV_INT_DOUBLE_64` | 0x27 | REGS | dest = (int64)src_a | Convert double to 64-bit integer (with rounding control) |
| 27 | `UOP_MOV_REG_PTR` | 0x28 | REGS, PTR | dest = *p | Load 32-bit value from memory pointer |
| 28 | `UOP_MOVZX_REG_PTR_8` | 0x29 | REGS, PTR | dest = *(uint8_t*)p | Zero-extending 8-bit load from pointer |
| 29 | `UOP_MOVZX_REG_PTR_16` | 0x2a | REGS, PTR | dest = *(uint16_t*)p | Zero-extending 16-bit load from pointer |

**Notes:**
- `UOP_MOV_REG_PTR`, `UOP_MOVZX_REG_PTR_8`, `UOP_MOVZX_REG_PTR_16` load
  directly from host memory pointers (NOT through emulated memory -- no segment
  check). Used to read cpu_state fields.
- `UOP_MOV_INT_DOUBLE` uses src_reg_b for new rounding control and src_reg_c
  for old rounding control (x87 rounding mode save/restore).
- `UOP_MOV_INT_DOUBLE_64` uses src_reg_a for the double, src_reg_b for the
  64-bit integer destination quad register, src_reg_c for the FPU tag.

---

## 5. Integer ALU

Arithmetic and logical operations on integer registers.

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 30 | `UOP_ADD` | 0x30 | REGS | dest = src_a + src_b | Integer addition |
| 31 | `UOP_ADD_IMM` | 0x31 | REGS, IMM | dest = src_a + imm | Integer addition with immediate |
| 32 | `UOP_AND` | 0x32 | REGS, IMM | dest = src_a & src_b | Bitwise AND |
| 33 | `UOP_AND_IMM` | 0x33 | REGS, IMM | dest = src_a & imm | Bitwise AND with immediate |
| 34 | `UOP_ADD_LSHIFT` | 0x34 | REGS, IMM | dest = src_a + (src_b << imm) | Add with left-shifted second operand (LEA) |
| 35 | `UOP_OR` | 0x35 | REGS, IMM | dest = src_a \| src_b | Bitwise OR |
| 36 | `UOP_OR_IMM` | 0x36 | REGS, IMM | dest = src_a \| imm | Bitwise OR with immediate |
| 37 | `UOP_SUB` | 0x37 | REGS | dest = src_a - src_b | Integer subtraction |
| 38 | `UOP_SUB_IMM` | 0x38 | REGS, IMM | dest = src_a - imm | Integer subtraction with immediate |
| 39 | `UOP_XOR` | 0x39 | REGS, IMM | dest = src_a ^ src_b | Bitwise XOR |
| 40 | `UOP_XOR_IMM` | 0x3a | REGS, IMM | dest = src_a ^ imm | Bitwise XOR with immediate |
| 41 | `UOP_ANDN` | 0x3b | REGS, IMM | dest = ~src_a & src_b | Bitwise AND-NOT |

**Notes:**
- `UOP_ADD_LSHIFT` maps directly to x86 LEA with scaled index. The immediate
  must be 0-3 (representing scale factors 1, 2, 4, 8). On ARM64 this maps to
  `ADD Rd, Rn, Rm, LSL #imm`.
- None of these ALU UOPs produce or consume flags directly. Flag computation
  is handled separately through IREG_flags_op/res/op1/op2 registers and the
  lazy flag evaluation system.
- `UOP_ANDN` is used for bit manipulation (e.g., clearing specific bits). On
  ARM64 it maps to the `BIC` instruction.

---

## 6. Shift / Rotate

Integer shift and rotate operations.

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 42 | `UOP_SAR` | 0x50 | REGS | dest = src_a >> src_b (arith) | Arithmetic right shift by register |
| 43 | `UOP_SAR_IMM` | 0x51 | REGS, IMM | dest = src_a >> imm (arith) | Arithmetic right shift by immediate |
| 44 | `UOP_SHL` | 0x52 | REGS | dest = src_a << src_b | Logical left shift by register |
| 45 | `UOP_SHL_IMM` | 0x53 | REGS, IMM | dest = src_a << imm | Logical left shift by immediate |
| 46 | `UOP_SHR` | 0x54 | REGS | dest = src_a >> src_b (logic) | Logical right shift by register |
| 47 | `UOP_SHR_IMM` | 0x55 | REGS, IMM | dest = src_a >> imm (logic) | Logical right shift by immediate |
| 48 | `UOP_ROL` | 0x56 | REGS | dest = src_a rotl src_b | Rotate left by register |
| 49 | `UOP_ROL_IMM` | 0x57 | REGS, IMM | dest = src_a rotl imm | Rotate left by immediate |
| 50 | `UOP_ROR` | 0x58 | REGS | dest = src_a rotr src_b | Rotate right by register |
| 51 | `UOP_ROR_IMM` | 0x59 | REGS, IMM | dest = src_a rotr imm | Rotate right by immediate |

**Notes:**
- Like ALU UOPs, shifts do not directly produce flags. Flag state is computed
  separately.
- Variable-count shifts (SAR/SHL/SHR by register) need special handling on
  ARM64 because ARM64 shift amounts are unsigned and x86 masks the count to
  5 or 6 bits depending on operand size.

---

## 7. Emulated Memory Access

These UOPs access the emulated machine's memory through the segment + address
translation layer. They are ORDER barriers because the memory access functions
may cause a page fault or other exception that exits the block.

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 52 | `UOP_MEM_LOAD_ABS` | 0x40 | REGS, IMM, ORDER | dest = seg:[imm] | Load from absolute address |
| 53 | `UOP_MEM_LOAD_REG` | 0x41 | REGS, IMM, ORDER | dest = seg:[reg+offset] | Load from register + offset |
| 54 | `UOP_MEM_STORE_ABS` | 0x42 | REGS, IMM, ORDER | seg:[imm] = src | Store to absolute address |
| 55 | `UOP_MEM_STORE_REG` | 0x43 | REGS, ORDER | seg:[reg] = src | Store to register address |
| 56 | `UOP_MEM_STORE_IMM_8` | 0x44 | REGS, IMM, ORDER | seg:[reg] = imm8 | Store 8-bit immediate to memory |
| 57 | `UOP_MEM_STORE_IMM_16` | 0x45 | REGS, IMM, ORDER | seg:[reg] = imm16 | Store 16-bit immediate to memory |
| 58 | `UOP_MEM_STORE_IMM_32` | 0x46 | REGS, IMM, ORDER | seg:[reg] = imm32 | Store 32-bit immediate to memory |
| 59 | `UOP_MEM_LOAD_SINGLE` | 0x47 | REGS, IMM, ORDER | dest = (float)seg:[reg] | Load single-precision float from memory |
| 60 | `UOP_MEM_LOAD_DOUBLE` | 0x49 | REGS, IMM, ORDER | dest = (double)seg:[reg] | Load double-precision float from memory |
| 61 | `UOP_MEM_STORE_SINGLE` | 0x4a | REGS, ORDER | seg:[reg] = (float)src | Store single-precision float to memory |
| 62 | `UOP_MEM_STORE_DOUBLE` | 0x4b | REGS, ORDER | seg:[reg] = (double)src | Store double-precision float to memory |

**Notes:**
- All memory UOPs are ORDER barriers. The memory access helper functions
  (e.g., `codegen_mem_load_long`) can trigger page faults, SMIs, or other
  exceptions. If an exception occurs, all register state must be consistent
  so the block can exit cleanly.
- `UOP_MEM_LOAD_ABS`/`UOP_MEM_STORE_ABS` use `src_reg_a` for the segment base
  and `imm_data` for the absolute offset.
- `UOP_MEM_LOAD_REG`/`UOP_MEM_STORE_REG` use `src_reg_a` for the segment base,
  `src_reg_b` for the address register, and `imm_data` for an optional offset.
- The `is_a16` flag on the UOP indicates 16-bit address mode (address wraps
  at 0xFFFF).

---

## 8. Compare + Branch (Block Exit)

These UOPs compare and conditionally exit the current block (jumping to an
absolute address, typically `codegen_exit_rout` or a GPF handler).

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 63 | `UOP_CMP_IMM_JZ` | 0x48 | REGS, IMM, PTR, ORDER | if (src_a == imm) goto p | Compare with immediate, jump to pointer if equal |
| 64 | `UOP_CMP_JB` | 0x4c | REGS, PTR, ORDER | if (src_a < src_b) goto p | Compare unsigned, jump if below |
| 65 | `UOP_CMP_JNBE` | 0x4d | REGS, PTR, ORDER | if (src_a > src_b) goto p | Compare unsigned, jump if above |

**Notes:**
- These are used for exception checking (e.g., segment limit violations,
  FPU/MMX availability checks).
- The pointer `p` is an absolute address to jump to if the condition is true
  (typically the block exit routine).

---

## 9. Compare + Branch (Intra-Block, with JUMP flag)

These UOPs perform conditional branches within a compiled block. They carry
the `UOP_TYPE_JUMP` flag so the backend can patch the branch offset when the
target UOP's address is known.

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 66 | `UOP_CMP_IMM_JZ_DEST` | 0x60 | REGS, IMM, PTR, ORDER, JUMP | if (src_a == imm) goto dest | CMP imm, jump if zero (within block) |
| 67 | `UOP_CMP_IMM_JNZ_DEST` | 0x61 | REGS, IMM, PTR, ORDER, JUMP | if (src_a != imm) goto dest | CMP imm, jump if not zero (within block) |
| 68 | `UOP_CMP_JB_DEST` | 0x62 | REGS, IMM, PTR, ORDER, JUMP | if (src_a < src_b) goto dest | CMP unsigned, jump if below |
| 69 | `UOP_CMP_JNB_DEST` | 0x63 | REGS, IMM, PTR, ORDER, JUMP | if (src_a >= src_b) goto dest | CMP unsigned, jump if not below |
| 70 | `UOP_CMP_JO_DEST` | 0x64 | REGS, IMM, PTR, ORDER, JUMP | if overflow goto dest | CMP, jump if overflow |
| 71 | `UOP_CMP_JNO_DEST` | 0x65 | REGS, IMM, PTR, ORDER, JUMP | if !overflow goto dest | CMP, jump if not overflow |
| 72 | `UOP_CMP_JZ_DEST` | 0x66 | REGS, IMM, PTR, ORDER, JUMP | if (src_a == src_b) goto dest | CMP, jump if equal |
| 73 | `UOP_CMP_JNZ_DEST` | 0x67 | REGS, IMM, PTR, ORDER, JUMP | if (src_a != src_b) goto dest | CMP, jump if not equal |
| 74 | `UOP_CMP_JL_DEST` | 0x68 | REGS, IMM, PTR, ORDER, JUMP | if signed(src_a < src_b) goto dest | CMP signed, jump if less |
| 75 | `UOP_CMP_JNL_DEST` | 0x69 | REGS, IMM, PTR, ORDER, JUMP | if signed(src_a >= src_b) goto dest | CMP signed, jump if not less |
| 76 | `UOP_CMP_JBE_DEST` | 0x6a | REGS, IMM, PTR, ORDER, JUMP | if (src_a <= src_b) goto dest | CMP unsigned, jump if below/equal |
| 77 | `UOP_CMP_JNBE_DEST` | 0x6b | REGS, IMM, PTR, ORDER, JUMP | if (src_a > src_b) goto dest | CMP unsigned, jump if above |
| 78 | `UOP_CMP_JLE_DEST` | 0x6c | REGS, IMM, PTR, ORDER, JUMP | if signed(src_a <= src_b) goto dest | CMP signed, jump if less/equal |
| 79 | `UOP_CMP_JNLE_DEST` | 0x6d | REGS, IMM, PTR, ORDER, JUMP | if signed(src_a > src_b) goto dest | CMP signed, jump if greater |

**Notes:**
- These implement x86 conditional jumps (Jcc) within a basic block or loop
  unroll.
- All are ORDER barriers because control flow changes require consistent state.
- The `jump_dest_uop` field links to the target UOP index. At code generation
  time, the backend writes the branch offset to `jump_dest`.
- Covers all x86 condition codes: B/NB (CF), Z/NZ (ZF), BE/NBE (CF|ZF),
  L/NL (SF!=OF), LE/NLE (ZF|SF!=OF), O/NO (OF).

---

## 10. Test + Branch (Intra-Block)

Test sign bit and conditionally branch within the block.

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 80 | `UOP_TEST_JNS_DEST` | 0x70 | REGS, IMM, PTR, ORDER, JUMP | if (src_a >= 0) goto dest | Jump if sign bit clear (positive) |
| 81 | `UOP_TEST_JS_DEST` | 0x71 | REGS, IMM, PTR, ORDER, JUMP | if (src_a < 0) goto dest | Jump if sign bit set (negative) |

**Notes:**
- Used for x86 JS/JNS conditional branches.
- Tests the sign bit (MSB) of src_reg_a without modifying it.

---

## 11. FPU (x87)

Floating-point arithmetic and control operations.

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 82 | `UOP_FP_ENTER` | 0x80 | IMM, BARRIER | (check FPU avail) | FPU availability check, must precede any FPU register access |
| 83 | `UOP_FADD` | 0x81 | REGS | dest = src_a + src_b | Double-precision addition |
| 84 | `UOP_FSUB` | 0x82 | REGS | dest = src_a - src_b | Double-precision subtraction |
| 85 | `UOP_FMUL` | 0x83 | REGS | dest = src_a * src_b | Double-precision multiplication |
| 86 | `UOP_FDIV` | 0x84 | REGS | dest = src_a / src_b | Double-precision division |
| 87 | `UOP_FCOM` | 0x85 | REGS | dest = compare(src_a, src_b) | FP compare, result to flags register |
| 88 | `UOP_FABS` | 0x86 | REGS | dest = fabs(src_a) | Absolute value |
| 89 | `UOP_FCHS` | 0x87 | REGS | dest = -src_a | Change sign (negate) |
| 90 | `UOP_FTST` | 0x88 | REGS | dest = compare(src_a, 0.0) | Test against zero, result to flags |
| 91 | `UOP_FSQRT` | 0x89 | REGS | dest = sqrt(src_a) | Square root |

**Notes:**
- `UOP_FP_ENTER` is a BARRIER and must be emitted before any FPU register
  access. On ARM64, it calls `codegen_fp_enter()` which checks the FPU
  availability (CR0.EM/TS bits) and may fault.
- `UOP_FCOM` and `UOP_FTST` produce x87-style condition codes (C0/C1/C2/C3)
  in the destination register, which are then stored to the FPU status word.
- The FPU operates on IREG_ST0..ST7 registers (double-precision, 64-bit).
- Missing from native JIT: FSIN, FCOS, FPTAN, FPATAN, FYL2X, FYL2XP1,
  F2XM1, FSCALE, FPREM, FPREM1 -- these all fall back to the interpreter.

---

## 12. MMX Enter

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 92 | `UOP_MMX_ENTER` | 0x90 | IMM, BARRIER | (check MMX avail) | MMX availability check, must precede any MMX register access |

**Notes:**
- Similar to `UOP_FP_ENTER`, but for MMX. Calls `codegen_mmx_enter()`.
- MMX and FPU are mutually exclusive -- entering MMX mode clears
  `codegen_fpu_entered` and vice versa.

---

## 13. MMX Packed Integer Arithmetic

All operate on 64-bit MMX registers (IREG_MM0..MM7) containing packed integers.

### Addition

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 93 | `UOP_PADDB` | 0x91 | REGS | dest = src_a + src_b | Packed byte add (8x8-bit) |
| 94 | `UOP_PADDW` | 0x92 | REGS | dest = src_a + src_b | Packed word add (4x16-bit) |
| 95 | `UOP_PADDD` | 0x93 | REGS | dest = src_a + src_b | Packed dword add (2x32-bit) |
| 96 | `UOP_PADDSB` | 0x94 | REGS | dest = src_a + src_b | Packed byte add, signed saturation |
| 97 | `UOP_PADDSW` | 0x95 | REGS | dest = src_a + src_b | Packed word add, signed saturation |
| 98 | `UOP_PADDUSB` | 0x96 | REGS | dest = src_a + src_b | Packed byte add, unsigned saturation |
| 99 | `UOP_PADDUSW` | 0x97 | REGS | dest = src_a + src_b | Packed word add, unsigned saturation |

### Subtraction

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 100 | `UOP_PSUBB` | 0x98 | REGS | dest = src_a - src_b | Packed byte subtract |
| 101 | `UOP_PSUBW` | 0x99 | REGS | dest = src_a - src_b | Packed word subtract |
| 102 | `UOP_PSUBD` | 0x9a | REGS | dest = src_a - src_b | Packed dword subtract |
| 103 | `UOP_PSUBSB` | 0x9b | REGS | dest = src_a - src_b | Packed byte subtract, signed saturation |
| 104 | `UOP_PSUBSW` | 0x9c | REGS | dest = src_a - src_b | Packed word subtract, signed saturation |
| 105 | `UOP_PSUBUSB` | 0x9d | REGS | dest = src_a - src_b | Packed byte subtract, unsigned saturation |
| 106 | `UOP_PSUBUSW` | 0x9e | REGS | dest = src_a - src_b | Packed word subtract, unsigned saturation |

---

## 14. MMX Packed Shift

Shift packed integers by immediate count. On ARM64, these map to NEON
SHL/SSHR/USHR instructions.

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 107 | `UOP_PSLLW_IMM` | 0x9f | REGS | dest = src_a << imm | Packed word shift left |
| 108 | `UOP_PSLLD_IMM` | 0xa0 | REGS | dest = src_a << imm | Packed dword shift left |
| 109 | `UOP_PSLLQ_IMM` | 0xa1 | REGS | dest = src_a << imm | Packed qword shift left |
| 110 | `UOP_PSRAW_IMM` | 0xa2 | REGS | dest = src_a >> imm | Packed word arithmetic shift right |
| 111 | `UOP_PSRAD_IMM` | 0xa3 | REGS | dest = src_a >> imm | Packed dword arithmetic shift right |
| 112 | `UOP_PSRAQ_IMM` | 0xa4 | REGS | dest = src_a >> imm | Packed qword arithmetic shift right |
| 113 | `UOP_PSRLW_IMM` | 0xa5 | REGS | dest = src_a >> imm | Packed word logical shift right |
| 114 | `UOP_PSRLD_IMM` | 0xa6 | REGS | dest = src_a >> imm | Packed dword logical shift right |
| 115 | `UOP_PSRLQ_IMM` | 0xa7 | REGS | dest = src_a >> imm | Packed qword logical shift right |

**Notes:**
- Only immediate-count shifts have UOPs. Variable-count shifts (PSLLW by MM
  register) fall back to the interpreter.
- Shift count overflow semantics: if shift >= element width, result is zero
  for logical shifts and sign-fill for arithmetic shifts (matching x86 MMX).

---

## 15. MMX Packed Compare

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 116 | `UOP_PCMPEQB` | 0xa8 | REGS | dest = (src_a == src_b) ? 0xFF : 0 | Packed byte compare equal |
| 117 | `UOP_PCMPEQW` | 0xa9 | REGS | dest = (src_a == src_b) ? 0xFFFF : 0 | Packed word compare equal |
| 118 | `UOP_PCMPEQD` | 0xaa | REGS | dest = (src_a == src_b) ? 0xFFFFFFFF : 0 | Packed dword compare equal |
| 119 | `UOP_PCMPGTB` | 0xab | REGS | dest = signed(src_a > src_b) ? 0xFF : 0 | Packed byte signed compare GT |
| 120 | `UOP_PCMPGTW` | 0xac | REGS | dest = signed(src_a > src_b) ? 0xFFFF : 0 | Packed word signed compare GT |
| 121 | `UOP_PCMPGTD` | 0xad | REGS | dest = signed(src_a > src_b) ? 0xFFFFFFFF : 0 | Packed dword signed compare GT |

---

## 16. MMX Pack / Unpack

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 122 | `UOP_PUNPCKLBW` | 0xae | REGS | dest = interleave_lo(src_a, src_b) | Unpack/interleave low bytes |
| 123 | `UOP_PUNPCKLWD` | 0xaf | REGS | dest = interleave_lo(src_a, src_b) | Unpack/interleave low words |
| 124 | `UOP_PUNPCKLDQ` | 0xb0 | REGS | dest = interleave_lo(src_a, src_b) | Unpack/interleave low dwords |
| 125 | `UOP_PUNPCKHBW` | 0xb1 | REGS | dest = interleave_hi(src_a, src_b) | Unpack/interleave high bytes |
| 126 | `UOP_PUNPCKHWD` | 0xb2 | REGS | dest = interleave_hi(src_a, src_b) | Unpack/interleave high words |
| 127 | `UOP_PUNPCKHDQ` | 0xb3 | REGS | dest = interleave_hi(src_a, src_b) | Unpack/interleave high dwords |
| 128 | `UOP_PACKSSWB` | 0xb4 | REGS | dest = pack(src_a, src_b) w->b signed | Pack words to bytes, signed saturation |
| 129 | `UOP_PACKSSDW` | 0xb5 | REGS | dest = pack(src_a, src_b) d->w signed | Pack dwords to words, signed saturation |
| 130 | `UOP_PACKUSWB` | 0xb6 | REGS | dest = pack(src_a, src_b) w->b unsigned | Pack words to bytes, unsigned saturation |

**Notes:**
- On ARM64, PUNPCKL* maps to ZIP1 and PUNPCKH* maps to ZIP2. Endianness is
  correct on little-endian ARM64 (verified in R7 of prior work).

---

## 17. MMX Multiply

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 131 | `UOP_PMULLW` | 0xb7 | REGS | dest = (src_a * src_b) & 0xFFFF | Packed word multiply, keep low 16 bits |
| 132 | `UOP_PMULHW` | 0xb8 | REGS | dest = (src_a * src_b) >> 16 | Packed word multiply, keep high 16 bits |
| 133 | `UOP_PMADDWD` | 0xb9 | REGS | dest = madd(src_a, src_b) | Packed multiply-add: multiply words, add adjacent pairs to dwords |

**Notes:**
- `UOP_PMADDWD` computes: `dest[0] = src_a[0]*src_b[0] + src_a[1]*src_b[1]`
  and `dest[1] = src_a[2]*src_b[2] + src_a[3]*src_b[3]`.
- These are multi-instruction sequences on ARM64 NEON (SMULL + shrink or
  SMLAL pairs), not kept as parametric macro handlers due to complexity.

---

## 18. 3DNow! Packed Float

AMD 3DNow! instructions operating on packed 32-bit floats in MMX registers
(2x float32 per 64-bit MMX register).

### Arithmetic

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 134 | `UOP_PFADD` | 0xba | REGS | dest = src_a + src_b | Packed float add |
| 135 | `UOP_PFSUB` | 0xbb | REGS | dest = src_a - src_b | Packed float subtract |
| 136 | `UOP_PFMUL` | 0xbc | REGS | dest = src_a * src_b | Packed float multiply |
| 137 | `UOP_PFMAX` | 0xbd | REGS | dest = max(src_a, src_b) | Packed float maximum |
| 138 | `UOP_PFMIN` | 0xbe | REGS | dest = min(src_a, src_b) | Packed float minimum |

### Compare

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 139 | `UOP_PFCMPEQ` | 0xbf | REGS | dest = (src_a == src_b) ? 0xFFFFFFFF : 0 | Packed float compare equal |
| 140 | `UOP_PFCMPGE` | 0xc0 | REGS | dest = (src_a >= src_b) ? 0xFFFFFFFF : 0 | Packed float compare greater/equal |
| 141 | `UOP_PFCMPGT` | 0xc1 | REGS | dest = (src_a > src_b) ? 0xFFFFFFFF : 0 | Packed float compare greater |

### Conversion

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 142 | `UOP_PF2ID` | 0xc2 | REGS | dest = (int32)src_a | Packed float to packed 32-bit int (truncate) |
| 143 | `UOP_PI2FD` | 0xc3 | REGS | dest = (float)src_a | Packed 32-bit int to packed float |

### Reciprocal / Reciprocal Square Root

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 144 | `UOP_PFRCP` | 0xc4 | REGS | dest[0]=dest[1] = 1.0/src[0] | Reciprocal estimate (~14-bit precision) |
| 145 | `UOP_PFRSQRT` | 0xc5 | REGS | dest[0]=dest[1] = 1.0/sqrt(src[0]) | Reciprocal sqrt estimate (~15-bit precision) |

**Notes:**
- On ARM64, PFRCP uses FRECPE + Newton-Raphson (FRECPS + FMUL) to achieve
  >= 14-bit precision. PFRSQRT uses FRSQRTE + Newton-Raphson (FRSQRTS + FMUL).
  See prior work Phase 1.
- Both produce the scalar result and duplicate it to both float lanes.
- 3DNow! float operations use the lower 64 bits of NEON V registers in
  V2S (vector of 2 singles) mode.

---

## 19. Debug

| # | UOP Name | Opcode | Flags | Operands | Description |
|---|----------|--------|-------|----------|-------------|
| 146 | `UOP_LOG_INSTR` | 0x1f | IMM | log(imm) | Log non-recompiled instruction (DEBUG_EXTRA only) |

**Notes:**
- Only available when `DEBUG_EXTRA` is defined at compile time.
- Used for debugging which instructions are not being JIT-compiled.

---

## Appendix: UOP Opcode Map

Compact view of all opcodes (lower 16 bits of `type` field):

```
0x00-0x03  LOAD_FUNC_ARG_0..3        (register -> arg)
0x08-0x0b  LOAD_FUNC_ARG_0..3_IMM    (immediate -> arg)
0x10       CALL_FUNC                  (call, no result)
0x11       CALL_INSTRUCTION_FUNC      (interpreter fallback)
0x12       STORE_P_IMM                (store 32-bit imm to ptr)
0x13       STORE_P_IMM_8              (store 8-bit imm to ptr)
0x14       LOAD_SEG                   (load segment)
0x15       JMP                        (unconditional jump)
0x16       CALL_FUNC_RESULT           (call, with result)
0x17       JMP_DEST                   (jump with dest offset)
0x18       NOP_BARRIER                (barrier, no-op)
0x19       STORE_P_IMM_16             (store 16-bit imm to ptr)
0x1f       LOG_INSTR                  (debug only)

0x20       MOV_PTR                    (dest = ptr)
0x21       MOV_IMM                    (dest = imm)
0x22       MOV                        (dest = src)
0x23       MOVZX                      (zero extend)
0x24       MOVSX                      (sign extend)
0x25       MOV_DOUBLE_INT             (int -> double)
0x26       MOV_INT_DOUBLE             (double -> int32)
0x27       MOV_INT_DOUBLE_64          (double -> int64)
0x28       MOV_REG_PTR                (dest = *ptr)
0x29       MOVZX_REG_PTR_8            (dest = *(u8*)ptr)
0x2a       MOVZX_REG_PTR_16           (dest = *(u16*)ptr)

0x30       ADD
0x31       ADD_IMM
0x32       AND
0x33       AND_IMM
0x34       ADD_LSHIFT                 (dest = a + (b << imm))
0x35       OR
0x36       OR_IMM
0x37       SUB
0x38       SUB_IMM
0x39       XOR
0x3a       XOR_IMM
0x3b       ANDN                       (dest = ~a & b)

0x40       MEM_LOAD_ABS
0x41       MEM_LOAD_REG
0x42       MEM_STORE_ABS
0x43       MEM_STORE_REG
0x44       MEM_STORE_IMM_8
0x45       MEM_STORE_IMM_16
0x46       MEM_STORE_IMM_32
0x47       MEM_LOAD_SINGLE
0x48       CMP_IMM_JZ                 (exit block if equal)
0x49       MEM_LOAD_DOUBLE
0x4a       MEM_STORE_SINGLE
0x4b       MEM_STORE_DOUBLE
0x4c       CMP_JB                     (exit block if below)
0x4d       CMP_JNBE                   (exit block if above)

0x50-0x59  SAR/SHL/SHR/ROL/ROR        (shifts and rotates)

0x60-0x6d  CMP_*_DEST                 (intra-block branches)
0x70-0x71  TEST_JNS/JS_DEST           (sign-test branches)

0x80       FP_ENTER
0x81-0x89  FADD/FSUB/FMUL/FDIV/FCOM/FABS/FCHS/FTST/FSQRT

0x90       MMX_ENTER
0x91-0x9e  PADDB..PSUBUSW             (packed int add/sub)
0x9f-0xa7  PSLLW..PSRLQ_IMM           (packed shifts)
0xa8-0xad  PCMPEQB..PCMPGTD           (packed compares)
0xae-0xb6  PUNPCKLBW..PACKUSWB        (pack/unpack)
0xb7-0xb9  PMULLW/PMULHW/PMADDWD      (packed multiply)

0xba-0xc5  PF*/PI2FD                  (3DNow! float)

0xc6       UOP_MAX                    (sentinel, not a real UOP)
0xff       UOP_INVALID                (invalid/unassigned)
```

---

## Appendix: Notable Gaps (Instructions Without Native UOPs)

The following x86 instruction categories fall back to the interpreter via
`UOP_CALL_INSTRUCTION_FUNC` because they have no dedicated UOP:

1. **x87 transcendentals**: FSIN, FCOS, FPTAN, FPATAN, FYL2X, FYL2XP1,
   F2XM1, FSCALE, FPREM, FPREM1
2. **x87 misc**: FXAM, FXTRACT, FINIT, FLDENV, FSTENV, FRSTOR, FSAVE,
   FDECSTP, FINCSTP, FFREE
3. **Integer multiply with flags**: IMUL (3-operand), MUL/IMUL with
   EDX:EAX result
4. **Integer divide**: DIV, IDIV
5. **Bit scan**: BSF, BSR
6. **String operations**: REP MOVSB/W/D, REP STOSB/W/D, REP CMPSB/W/D,
   REP SCASB/W/D, REP LODSB/W/D
7. **BCD arithmetic**: AAA, AAS, AAM, AAD, DAA, DAS
8. **Misc**: XCHG, CMPXCHG, XADD, BSWAP, BT/BTS/BTR/BTC, SHLD, SHRD,
   BOUND, ENTER, LEAVE (complex forms)
9. **MMX variable shifts**: PSLLW/D/Q by register (only IMM forms have UOPs)
10. **Segment operations**: Complex forms of LDS, LES, LFS, LGS, LSS
11. **System**: LGDT, LIDT, SGDT, SIDT, LLDT, SLDT, LTR, STR, etc.

This gap analysis should be validated by the cpu-x86ops instruction coverage
audit.
