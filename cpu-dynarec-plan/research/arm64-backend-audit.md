# ARM64 Backend Audit -- 86Box New Dynarec

**Date**: 2026-03-03
**Files Audited**:
- `src/codegen_new/codegen_backend_arm64.c` (backend init, prologue/epilogue, load/store routines)
- `src/codegen_new/codegen_backend_arm64.h` (block/hash sizes)
- `src/codegen_new/codegen_backend_arm64_defs.h` (register definitions)
- `src/codegen_new/codegen_backend_arm64_ops.c` (ARM64 instruction emitters, ~1542 lines)
- `src/codegen_new/codegen_backend_arm64_ops.h` (emitter declarations, range-check macros)
- `src/codegen_new/codegen_backend_arm64_uops.c` (UOP handler implementations, ~3400 lines)
- `src/codegen_new/codegen_backend_arm64_imm.c` (logical immediate encoding table)
- `src/codegen_new/codegen_ir_defs.h` (UOP definitions, 0x00-0xc5)
- `src/codegen_new/codegen_allocator.c` (code buffer allocation, I-cache flush)
- `src/unix/unix.c` (plat_mmap, MAP_JIT)

---

## 1. UOP Coverage

### 1.1 Fully Native (register-to-register ARM64 instructions)

| UOP | ARM64 Instruction(s) | Notes |
|-----|----------------------|-------|
| `UOP_ADD` | `ADD Wd, Wn, Wm` | Sub-register (B, BH, W) uses BFI merge |
| `UOP_ADD_IMM` | `ADD Wd, Wn, #imm` | Two-step ADD for 24-bit immediates |
| `UOP_ADD_LSHIFT` | `ADD Wd, Wn, Wm, LSL #imm` | Direct barrel shifter exploit |
| `UOP_AND` | `AND Wd, Wn, Wm` / `AND_V` | Vector path for Q-size |
| `UOP_AND_IMM` | `AND Wd, Wn, #imm` | Uses logical immediate encoding |
| `UOP_ANDN` | `BIC_V` (vector) / `AND + NOT` sequence | Vector only for Q-size |
| `UOP_OR` | `ORR Wd, Wn, Wm` / `ORR_V` | Vector path for Q-size |
| `UOP_OR_IMM` | `ORR Wd, Wn, #imm` | Uses logical immediate encoding |
| `UOP_SUB` | `SUB Wd, Wn, Wm` | Sub-register via BFI |
| `UOP_SUB_IMM` | `SUB Wd, Wn, #imm` | |
| `UOP_XOR` | `EOR Wd, Wn, Wm` / `EOR_V` | Vector path for Q-size |
| `UOP_XOR_IMM` | `EOR Wd, Wn, #imm` | Uses logical immediate encoding |
| `UOP_MOV` | `ORR Wd, WZR, Wm` / `FMOV` | Multiple size variants |
| `UOP_MOV_IMM` | `MOVZ`/`MOVK` | 32-bit via `host_arm64_mov_imm` |
| `UOP_MOV_PTR` | `MOVX_IMM` (64-bit) | Up to 4 instructions for pointer |
| `UOP_MOVSX` | `SBFX` / `SXTB` / `SXTH` | Sign extension |
| `UOP_MOVZX` | `UBFX` / `AND` mask | Zero extension |
| `UOP_MOV_REG_PTR` | `MOVX_IMM` + `LDR` | Load from absolute address |
| `UOP_MOVZX_REG_PTR_8` | `MOVX_IMM` + `LDRB` | Byte load from absolute address |
| `UOP_MOVZX_REG_PTR_16` | `MOVX_IMM` + `LDRH` | Half-word load from absolute address |
| `UOP_SAR` | `ASR Wd, Wn, Wm` | Variable shift |
| `UOP_SAR_IMM` | `SBFX` (bitfield extract) | Immediate shift |
| `UOP_SHL` | `LSL Wd, Wn, Wm` | Variable shift |
| `UOP_SHL_IMM` | `ORR + LSL` (shift encoding) | Immediate shift |
| `UOP_SHR` | `LSR Wd, Wn, Wm` | Variable shift |
| `UOP_SHR_IMM` | `UBFX` (bitfield extract) | Immediate shift |
| `UOP_ROL` | `NEG` + `ROR` | ARM64 lacks native ROL |
| `UOP_ROL_IMM` | `ROR #(32-imm)` | Inverted rotate |
| `UOP_ROR` | `ROR Wd, Wn, Wm` | |
| `UOP_ROR_IMM` | `ORR + ROR` | Immediate rotate |
| `UOP_NOP_BARRIER` | (no code emitted) | |

### 1.2 FPU (Scalar D-register, all native)

| UOP | ARM64 Instruction | Notes |
|-----|-------------------|-------|
| `UOP_FADD` | `FADD Dd, Dn, Dm` | Double-precision |
| `UOP_FSUB` | `FSUB Dd, Dn, Dm` | |
| `UOP_FMUL` | `FMUL Dd, Dn, Dm` | |
| `UOP_FDIV` | `FDIV Dd, Dn, Dm` | |
| `UOP_FABS` | `FABS Dd, Dn` | |
| `UOP_FCHS` | `FNEG Dd, Dn` | |
| `UOP_FSQRT` | `FSQRT Dd, Dn` | |
| `UOP_FTST` | `FCMP Dd, #0` + 3x `CSEL` | Compare-to-zero, maps NV/Z/C flags |
| `UOP_FCOM` | `FCMP Dd, Dm` + 3x `CSEL` | Same CSEL pattern as FTST |
| `UOP_MOV_DOUBLE_INT` | `SCVTF Dd, Wn` / `SCVTF Dd, Xn` | Int-to-double |
| `UOP_MOV_INT_DOUBLE` | `BL codegen_fp_round` | Indirect jump table for rounding mode |
| `UOP_MOV_INT_DOUBLE_64` | `BL codegen_fp_round_quad` | 64-bit variant |

### 1.3 MMX (NEON V8B/V4H/V2S, all native)

| UOP | NEON Instruction | Notes |
|-----|-----------------|-------|
| `UOP_PADDB/W/D` | `ADD V8B/V4H/V2S` | |
| `UOP_PSUBB/W/D` | `SUB V8B/V4H/V2S` | |
| `UOP_PADDSB/W` | `SQADD V8B/V4H` | Signed saturation |
| `UOP_PADDUSB/W` | `UQADD V8B/V4H` | Unsigned saturation |
| `UOP_PSUBSB/W` | `SQSUB V8B/V4H` | |
| `UOP_PSUBUSB/W` | `UQSUB V8B/V4H` | |
| `UOP_PCMPEQB/W/D` | `CMEQ V8B/V4H/V2S` | |
| `UOP_PCMPGTB/W/D` | `CMGT V8B/V4H/V2S` | |
| `UOP_PMULLW` | `MUL V4H` | Low 16 bits of multiply |
| `UOP_PMULHW` | `SMULL V4S + SHRN V4H` | Widening mul, then narrow |
| `UOP_PMADDWD` | `SMULL + SADDLP` | Widening mul + pairwise add |
| `UOP_PACKSSWB` | `INS_D + SQXTN V8B` | Concatenate then narrow |
| `UOP_PACKSSDW` | `INS_D + SQXTN V4H` | |
| `UOP_PACKUSWB` | `INS_D + SQXTUN V8B` | Unsigned saturation |
| `UOP_PUNPCKLBW/WD/DQ` | `ZIP1 V8B/V4H/V2S` | Low interleave |
| `UOP_PUNPCKHBW/WD/DQ` | `ZIP2 V8B/V4H/V2S` | High interleave |
| `UOP_PSLLW/D/Q_IMM` | `SHL V4H/V2S/V2D` | Immediate shift left |
| `UOP_PSRLW/D/Q_IMM` | `USHR V4H/V2S/V2D` | Unsigned shift right |
| `UOP_PSRAW/D_IMM` | `SSHR V4H/V2S` | Signed shift right |

### 1.4 3DNow! (NEON V2S float, all native)

| UOP | NEON Instruction | Notes |
|-----|-----------------|-------|
| `UOP_PFADD` | `FADD V2S` | |
| `UOP_PFSUB` | `FSUB V2S` | |
| `UOP_PFMUL` | `FMUL V2S` | |
| `UOP_PFMAX` | `FMAX V2S` | |
| `UOP_PFMIN` | `FMIN V2S` | |
| `UOP_PFCMPEQ` | `FCMEQ V2S` | |
| `UOP_PFCMPGE` | `FCMGE V2S` | |
| `UOP_PFCMPGT` | `FCMGT V2S` | |
| `UOP_PF2ID` | `FCVTZS V2S` | Float-to-int |
| `UOP_PI2FD` | `SCVTF V2S` | Int-to-float |
| `UOP_PFRCP` | `FMOV_S_ONE + FDIV_S + DUP` | **Suboptimal** (see section 2) |
| `UOP_PFRSQRT` | `FSQRT_S + FMOV_S_ONE + FDIV_S + DUP` | **BUG + Suboptimal** (see section 2) |

### 1.5 Helper Calls (call out to C)

| UOP | Helper Function | Notes |
|-----|----------------|-------|
| `UOP_CALL_FUNC` | User-specified `uop->p` | `MOVX_IMM + BLR` |
| `UOP_CALL_FUNC_RESULT` | User-specified `uop->p` | Same, saves result reg |
| `UOP_CALL_INSTRUCTION_FUNC` | User-specified `uop->p` | Passes fetchdat, checks return |
| `UOP_LOAD_SEG` | `loadseg()` | Segment load with abort check |
| `UOP_FP_ENTER` | `x86_int(7)` on fault | Checks CR0.EM/TS bits |
| `UOP_MMX_ENTER` | `x86_int(7)` on fault | Same, then initializes MMX state |
| `UOP_MOV_INT_DOUBLE` | `codegen_fp_round` | Indirect jump to rounding routine |
| `UOP_MEM_LOAD_*` | `codegen_mem_load_*` | TLB fast path, fallback to readmem* |
| `UOP_MEM_STORE_*` | `codegen_mem_store_*` | TLB fast path, fallback to writemem* |

### 1.6 Unimplemented (fatal stubs)

| UOP | Status | File:Line |
|-----|--------|-----------|
| `UOP_LOAD_FUNC_ARG_1` | `fatal("codegen_LOAD_FUNC_ARG1")` | uops.c:834 |
| `UOP_LOAD_FUNC_ARG_2` | `fatal("codegen_LOAD_FUNC_ARG2")` | uops.c:840 |
| `UOP_LOAD_FUNC_ARG_3` | `fatal("codegen_LOAD_FUNC_ARG3")` | uops.c:846 |
| `UOP_PSRAQ_IMM` | Not in dispatch table | ir_defs.h:258, no handler |

**Note**: `UOP_LOAD_FUNC_ARG_0` is only partially implemented -- handles `IREG_SIZE_W` only (uops.c:824-827). Other sizes (L, B) would fatal.

### 1.7 Missing from dispatch table

The following UOPs are defined in `codegen_ir_defs.h` but have no entry in the `uop_handlers[]` array:

| UOP | Value | Notes |
|-----|-------|-------|
| `UOP_NOP_BARRIER` (0x18) | Null handler | May be intentional (no-op) |
| `UOP_JMP_DEST` (0x17) | No handler | Jump destination marker |
| `UOP_STORE_P_IMM_16` (0x19) | No handler | 16-bit pointer store |
| `UOP_PSRAQ_IMM` (0xa4) | No handler | Packed signed arithmetic shift right |

---

## 2. Suboptimal Code Sequences

### 2.1 BUG: PFRSQRT Overwrites FSQRT Result

**File**: `codegen_backend_arm64_uops.c`, lines 1876-1881

```c
host_arm64_FSQRT_S(block, REG_V_TEMP, src_reg_a);  // V_TEMP = sqrt(src[0])
host_arm64_FMOV_S_ONE(block, REG_V_TEMP);           // V_TEMP = 1.0  <-- OVERWRITES SQRT!
host_arm64_FDIV_S(block, dest_reg, dest_reg, REG_V_TEMP); // dest = dest/1.0 = dest (wrong!)
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

The `FMOV_S_ONE` on line 1879 destroys the FSQRT result stored in `REG_V_TEMP`. The correct sequence should be:

```c
host_arm64_FSQRT_S(block, REG_V_TEMP, src_reg_a);  // V_TEMP = sqrt(src[0])
host_arm64_FMOV_S_ONE(block, dest_reg);             // dest = 1.0
host_arm64_FDIV_S(block, dest_reg, dest_reg, REG_V_TEMP); // dest = 1.0/sqrt(src[0])
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

**Severity**: High -- PFRSQRT will produce incorrect results for 3DNow! games.

### 2.2 Conditional Branch Pattern: 2 Instructions Instead of 1

**File**: `codegen_backend_arm64_ops.c`, lines 468-578

Every conditional branch emitter (e.g., `host_arm64_BEQ_`) emits **two instructions**:

```c
uint32_t *
host_arm64_BEQ_(codeblock_t *block)
{
    codegen_alloc(block, 12);
    codegen_addlong(block, OPCODE_BCOND | COND_NE | OFFSET19(8));  // B.NE skip
    codegen_addlong(block, OPCODE_B);                                // B <target>
    return (uint32_t *) &block_write_data[block_pos - 4];
}
```

This pattern uses an **inverted** condition to skip over an unconditional `B` with 26-bit range. The motivation is clear: `B.cond` has only a 19-bit offset (+/-1MB), while `B` has a 26-bit offset (+/-128MB).

**Impact**: For intra-block jumps (which are always within a few KB), this doubles the branch cost. The 19-bit range of `B.cond` (+/-1MB) is more than sufficient for all intra-block branches. Only the unconditional `B` (for inter-block/exit) actually needs 26-bit range.

**Recommendation**: Add a "near" variant that emits a single `B.cond` for known-short-range branches. The existing 2-instruction pattern should be reserved for far branches only.

### 2.3 Call Sequence: MOVX_IMM + BLR (up to 5 instructions)

**File**: `codegen_backend_arm64_ops.c`, lines 1518-1522

```c
void
host_arm64_call(codeblock_t *block, void *dst_addr)
{
    host_arm64_MOVX_IMM(block, REG_X16, (uint64_t) dst_addr);
    host_arm64_BLR(block, REG_X16);
}
```

Where `MOVX_IMM` (lines 1071-1080) always starts with `MOVZ` and conditionally adds up to 3 `MOVK` instructions:

```c
void
host_arm64_MOVX_IMM(codeblock_t *block, int reg, uint64_t imm_data)
{
    codegen_addlong(block, OPCODE_MOVZ_X | MOV_WIDE_HW(0) | IMM16(imm_data & 0xffff) | Rd(reg));
    if ((imm_data >> 16) & 0xffff)
        codegen_addlong(block, OPCODE_MOVK_X | MOV_WIDE_HW(1) | IMM16((imm_data >> 16) & 0xffff) | Rd(reg));
    if ((imm_data >> 32) & 0xffff)
        codegen_addlong(block, OPCODE_MOVK_X | MOV_WIDE_HW(2) | IMM16((imm_data >> 32) & 0xffff) | Rd(reg));
    if ((imm_data >> 48) & 0xffff)
        codegen_addlong(block, OPCODE_MOVK_X | MOV_WIDE_HW(3) | IMM16((imm_data >> 48) & 0xffff) | Rd(reg));
}
```

For typical macOS userspace addresses (e.g., `0x0001xxxx_xxxxxxxx`), this is 4+1 = 5 instructions per call. Every call site pays this penalty.

**Optimizations available**:
1. **MOVN optimization**: For values with many set bits (e.g., `0xFFFF_FFFF_FFFF_xxxx`), use `MOVN` + `MOVK` -- fewer instructions when upper halfwords are `0xFFFF`.
2. **ADRP+ADD**: If the target is within +/-4GB of the JIT code buffer, use `ADRP X16, target@PAGE; ADD X16, X16, target@PAGEOFF` -- always exactly 2 instructions.
3. **Literal pool**: Store target addresses in a nearby literal pool, then `LDR X16, [PC, #offset]` -- 1 instruction + data.
4. **Direct BL**: If the target is within +/-128MB of the JIT code, a single `BL target` suffices -- 1 instruction. This is feasible if the helper routines are in the same code buffer as the JIT code (which the load/store routines already are).

### 2.4 MOVX_IMM Never Uses MOVN

**File**: `codegen_backend_arm64_ops.c`, lines 1071-1080

The `MOVX_IMM` function always starts with `MOVZ` (zero-then-move) and skips halfwords that are zero. It never considers `MOVN` (move-wide-with-NOT) which would be better when most halfwords are `0xFFFF`:

- `0xFFFFFFFF_FFFFxxxx` with MOVZ: `MOVZ + MOVK + MOVK + MOVK` = 4 instructions
- `0xFFFFFFFF_FFFFxxxx` with MOVN: `MOVN #~xxxx` = 1 instruction

Typical kernel-space or high-range userspace addresses would benefit significantly.

### 2.5 Sub-Register Operations: Extra Instructions

Throughout `codegen_backend_arm64_uops.c`, sub-register operations (byte, byte-high, word) require extra instructions:

- **B (byte) ops**: Operate in full register, then `BFI dest, TEMP, 0, 8` to merge result
- **BH (byte-high) ops**: Often need `LSR #8` before operate, then `BFI dest, TEMP, 8, 8`
- **W (word) ops**: Operate in full register, then `BFI dest, TEMP, 0, 16`

Example from `codegen_ADD` (uops.c:44-46):
```c
host_arm64_ADD_REG(block, REG_TEMP, src_reg_a, src_reg_b, 0);
host_arm64_BFI(block, dest_reg, REG_TEMP, 0, 16);
```

This is 2 instructions per W-size ALU op vs 1 for L-size. The overhead is fundamental to how ARM64 handles sub-register insertion -- there is no way to avoid it without changing the register allocation strategy (e.g., dedicating separate registers per sub-register and reconstructing on spill).

### 2.6 Comparison for W and B Sizes: Extra MOV+CMP

**File**: `codegen_backend_arm64_uops.c`, lines 329-334

For W-size and B-size comparisons in `CMP_JNB_DEST` and similar:

```c
host_arm64_MOV_REG(block, REG_TEMP, src_reg_a, 16);    // LSL #16
host_arm64_CMP_REG_LSL(block, REG_TEMP, src_reg_b, 16); // CMP shifted
```

This left-shifts both operands to the top of the 32-bit word so the unsigned comparison works correctly. It costs 2 instructions instead of 1. An alternative would be to use `AND #0xFFFF` + `CMP`, which is also 2 instructions but may have better pipeline behavior on some microarchitectures.

### 2.7 No MADD/MSUB/MUL for Integer Operations

The backend has no integer multiply instruction. There is no `UOP_MUL` or `UOP_IMUL` defined in the IR. All integer multiplications go through `CALL_FUNC` to C helper functions. This is a significant gap for LEA-like operations and `IMUL r, r/m, imm`.

### 2.8 No CSINC/CSINV/CSNEG

Only `CSEL_CC`, `CSEL_EQ`, and `CSEL_VS` are implemented (ops.c:704-717). The ARM64 family includes:
- `CSINC` (conditional select increment) -- useful for SETcc-like patterns
- `CSINV` (conditional select invert) -- useful for conditional NOT
- `CSNEG` (conditional select negate) -- useful for conditional NEG

These are not currently used. Adding them would improve x86 SETcc and CMOVcc translation.

### 2.9 No LDP/STP Pairing for Adjacent cpu_state Accesses

The backend never uses `LDP/STP` for loading/storing adjacent fields in `cpu_state`. For example, `codegen_MMX_ENTER` (uops.c:800-803) does two separate `STR_IMM_W` to `cpu_state.tag[0]` and `cpu_state.tag[4]`, which are adjacent 4-byte fields. A single `STP W, W, [X29, #offset]` would halve the instruction count.

### 2.10 FP Rounding Via Indirect Jump Table

**File**: `codegen_backend_arm64.c`, lines 240-281

The `build_fp_round_routine` builds a 4-entry jump table indexed by rounding mode:

```c
host_arm64_LDR_IMM_W(block, REG_TEMP, REG_CPUSTATE, ... &cpu_state.new_fp_control ...);
host_arm64_ADR(block, REG_TEMP2, 12);
host_arm64_LDR_REG_X(block, REG_TEMP2, REG_TEMP2, REG_TEMP);
host_arm64_BR(block, REG_TEMP2);
// ... 4 absolute 64-bit addresses ...
// ... 4 code entries (FCVTNS/FCVTPS/FCVTMS/FCVTZS + RET each) ...
```

This is 4 instructions + indirect branch + 32 bytes of data per call. The indirect branch causes a pipeline flush. An alternative approach:
- Set FPCR rounding mode, then use `FRINTX_D` + `FCVTZS` -- 2-3 instructions but avoids the indirect branch
- Or use `FRINT32X`/`FRINT64X` (ARMv8.5) on newer cores

---

## 3. Immediate Handling

### 3.1 Logical Immediate Encoding

**File**: `codegen_backend_arm64_imm.c`

The backend uses a 1302-entry sorted lookup table for ARM64 logical immediate encoding. The search function `host_arm64_find_imm()` performs binary search over this table. If the immediate is found, it returns the 12-bit encoding; otherwise, it returns 0.

When an immediate cannot be encoded as a logical immediate, the fallback path loads the value into `REG_W16` (the intra-procedure-call scratch register) via `host_arm64_mov_imm()`:

```c
void
host_arm64_AND_IMM(codeblock_t *block, int dst_reg, int src_n_reg, uint32_t imm_data)
{
    uint32_t imm_encoding = host_arm64_find_imm(imm_data);
    if (imm_encoding) {
        codegen_addlong(block, OPCODE_AND_IMM | ... | IMM_LOGICAL(imm_encoding));
    } else {
        host_arm64_mov_imm(block, REG_W16, imm_data);
        codegen_addlong(block, OPCODE_AND_LSL | ... | Rm(REG_W16) | ...);
    }
}
```

This pattern is consistent across `AND_IMM`, `ANDS_IMM` (TST), `ORR_IMM`, and `EOR_IMM`. The implementation is correct.

### 3.2 ADD/SUB Immediate Encoding

**File**: `codegen_backend_arm64_ops.c`, lines 321-338

`host_arm64_ADD_IMM` handles multiple cases:
1. **Zero**: Uses `MOV_REG` (no-op if same register)
2. **Negative**: Redirects to `SUB_IMM`
3. **12-bit**: Single `ADD_IMM` with no shift
4. **24-bit**: Two `ADD_IMM` instructions (low 12 bits unshifted, high 12 bits shifted by 12)
5. **>24-bit**: Falls back to `MOVZ + MOVK + ADD_REG`

This is a good approach. The 24-bit case (two ADD_IMMs) is clever and handles the common case of offsets up to 16MB.

### 3.3 CMP/CMN Immediate Auto-Negation

The `CMP_IMM` and `CMN_IMM` functions automatically redirect negative immediates to each other:

```c
void host_arm64_CMP_IMM(codeblock_t *block, int src_n_reg, uint32_t imm_data) {
    if ((int32_t) imm_data < 0 && imm_data != (1ull << 31))
        host_arm64_CMN_IMM(block, src_n_reg, -(int32_t) imm_data);
    ...
}
```

This is correct ARM64 behavior -- `CMP X, #-5` is equivalent to `CMN X, #5`.

### 3.4 32-bit MOV Immediate

`host_arm64_mov_imm()` (ops.c:1532-1540) handles 32-bit immediates:
- If fits in single 16-bit halfword: 1x `MOVZ`
- Otherwise: `MOVZ` (low 16) + `MOVK` (high 16) = 2 instructions

This is efficient for 32-bit values. No further optimization needed.

### 3.5 LOAD_FUNC_ARG_*_IMM Uses MOVX_IMM for 32-bit Values

**File**: `codegen_backend_arm64_uops.c`, lines 851-853

```c
static int
codegen_LOAD_FUNC_ARG0_IMM(codeblock_t *block, uop_t *uop)
{
    host_arm64_MOVX_IMM(block, REG_ARG0, uop->imm_data);
    ...
}
```

On ARM64, `uop->imm_data` is `uintptr_t` (64-bit), but most immediate arguments are small values. `MOVX_IMM` always starts with a 64-bit `MOVZ`, meaning all small values take at least 1 instruction. However, for small values that fit in 16 bits, this is fine. The issue is that it uses the 64-bit `MOVZ_X` opcode even when a 32-bit `MOVZ_W` would suffice (32-bit MOVZ implicitly zero-extends the upper 32 bits of the X register).

---

## 4. NEON/ASIMD Opportunities

### 4.1 Current NEON Usage

The backend makes extensive and correct use of NEON for MMX and 3DNow!:

**MMX** uses 64-bit (D-register / lower half of V-register) operations:
- `V8B` for byte operations (PADDB, PSUBB, PCMPEQB, etc.)
- `V4H` for halfword operations (PADDW, PMULLW, PSLLW, etc.)
- `V2S` for word operations (PADDD, PSLLD, etc.)
- `V2D` for quadword operations (PSLLQ, PSRLQ)

**3DNow!** uses `V2S` (2x float32) for all packed float operations.

**x87 FPU** uses scalar D-register operations (FADD_D, FMUL_D, etc.).

### 4.2 PFRCP: Could Use VRECPE/VRECPS

**File**: `codegen_backend_arm64_uops.c`, lines 1858-1863

Current code (3 instructions, ~30 cycle latency for FDIV):
```c
host_arm64_FMOV_S_ONE(block, REG_V_TEMP);       // 1 cycle
host_arm64_FDIV_S(block, dest_reg, REG_V_TEMP, src_reg_a); // ~10-30 cycles
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0); // 1 cycle
```

Optimized version using VRECPE + VRECPS (Newton-Raphson step, ~8 cycles):
```c
FRECPE V_TEMP.2S, src.2S     // initial estimate, ~12 bits accuracy
FRECPS V_TEMP2.2S, V_TEMP.2S, src.2S  // refinement step
FMUL   V_TEMP.2S, V_TEMP.2S, V_TEMP2.2S  // apply correction
// Optional: repeat FRECPS+FMUL for more accuracy
DUP    dest.2S, V_TEMP.S[0]
```

3DNow! PFRCP only requires ~15 bits of precision (single-float mantissa is 23 bits, but 3DNow! is explicitly lower precision). One Newton-Raphson step after FRECPE should suffice.

The existing TODO comment acknowledges this.

### 4.3 PFRSQRT: Could Use VRSQRTE/VRSQRTS

**File**: `codegen_backend_arm64_uops.c`, lines 1876-1882

This has the same issue as PFRCP, plus the **bug** described in section 2.1. Even after fixing the bug, the FSQRT+FDIV sequence is very expensive (~30-60 cycles). Using FRSQRTE + FRSQRTS (Newton-Raphson) would be ~8 cycles.

### 4.4 No SSE Support

The IR defines no SSE UOPs. This is appropriate for 86Box's target hardware (up to Pentium III era). If SSE support were needed in the future, the NEON infrastructure is well-structured to support 128-bit `V4S` operations.

### 4.5 PMADDWD Could Use SMLAL

**File**: `codegen_backend_arm64_uops.c`, lines 1944-1964

Current PMADDWD implementation:
```c
host_arm64_SMULL_V4S_4H(block, REG_V_TEMP, src_reg_a, src_reg_b);
host_arm64_SADDLP_V2S_4H(block, REG_V_TEMP, REG_V_TEMP);  // BUG? SADDLP on V4S output
```

Wait -- there is a potential issue here. `SMULL V4S, V4H, V4H` produces 4x 32-bit results. Then `SADDLP V2S, V4H` performs pairwise add on 4x 16-bit to 2x 32-bit. But the input to SADDLP should be V4S (4x 32-bit) and the output should be V2D (2x 64-bit)... The instruction used may not match the intended operation. This needs verification against the specific NEON instruction encoding used (OPCODE_SADDLP_V2S_4H).

---

## 5. I-Cache and W^X Handling

### 5.1 I-Cache Invalidation

**File**: `codegen_allocator.c`, lines 190-202

```c
void
codegen_allocator_clean_blocks(UNUSED(struct mem_block_t *block))
{
#if defined __ARM_EABI__ || defined __aarch64__ || defined _M_ARM64
    while (1) {
        __clear_cache(&mem_block_alloc[block->offset],
                      &mem_block_alloc[block->offset + MEM_BLOCK_SIZE]);
        if (block->next)
            block = &mem_blocks[block->next - 1];
        else
            break;
    }
#endif
}
```

**Issues**:
1. **Flush granularity**: Flushes the entire `MEM_BLOCK_SIZE` (defined in `codegen_allocator.c`), even if only a fraction was modified. This wastes time flushing cache lines that were not written.
2. **No narrow invalidation**: The flush covers the full block, not just the modified range `[block_start, block_pos]`.
3. **Multiple blocks**: If a codeblock spans multiple mem_blocks (via `codegen_allocate_new_block`), all blocks are flushed in a loop, which is correct.

**Recommendation**: Pass the actual written range to the flush function:
```c
__clear_cache(&mem_block_alloc[block->offset],
              &mem_block_alloc[block->offset + actual_bytes_written]);
```

### 5.2 Apple Silicon W^X: MAP_JIT Without pthread_jit_write_protect_np

**File**: `src/unix/unix.c`, lines 437-438

```c
#if defined __APPLE__ && defined MAP_JIT
    void *ret = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_ANON | MAP_PRIVATE | MAP_JIT, -1, 0);
```

On Apple Silicon, `MAP_JIT` allows a region to be both writable and executable, but Apple's documentation states that `pthread_jit_write_protect_np()` should be used to toggle between write and execute modes on a per-thread basis:

```c
pthread_jit_write_protect_np(false);  // Enable writes
// ... write JIT code ...
pthread_jit_write_protect_np(true);   // Enable execution
sys_icache_invalidate(addr, size);    // Flush I-cache
```

**Current state**: The codebase uses `MAP_JIT` but **never calls `pthread_jit_write_protect_np()`**. There are also **no calls to `sys_icache_invalidate()`** -- only `__clear_cache()` (GCC builtin).

**Why it works anyway**: On macOS, when using `MAP_JIT`, `pthread_jit_write_protect_np()` is optional for single-threaded JIT write access. The hardware allows RWX pages with MAP_JIT. However:
1. Apple's Hardened Runtime (which is required for notarized apps) enforces W^X more strictly. If 86Box is ever code-signed with hardened runtime entitlements, this will need to be fixed.
2. The `__clear_cache()` builtin typically calls `sys_icache_invalidate()` on Apple platforms, so that part is likely fine.
3. On newer macOS versions, Apple may tighten enforcement.

**Recommendation**: Add `pthread_jit_write_protect_np()` calls around JIT code generation to be future-proof. This requires:
- `pthread_jit_write_protect_np(0)` before writing to the code buffer
- `pthread_jit_write_protect_np(1)` after writing and before execution
- Use `sys_icache_invalidate()` explicitly on Apple instead of `__clear_cache()`

### 5.3 Linux ARM64 W^X

On Linux with the non-Apple path:

```c
void *ret = mmap(0, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_ANON | MAP_PRIVATE, -1, 0);
```

This requests simultaneous RWX, which is allowed by default on Linux but may be restricted by:
- SELinux with `deny_execmem`
- seccomp sandboxes
- Hardened kernels

For better W^X compliance on Linux, the code should use `mprotect()` to toggle between PROT_WRITE and PROT_EXEC.

---

## 6. Block Entry/Exit

### 6.1 Prologue

**File**: `codegen_backend_arm64.c`, lines 344-363

```c
void
codegen_backend_prologue(codeblock_t *block)
{
    block_pos = BLOCK_START;

    host_arm64_STP_PREIDX_X(block, REG_X29, REG_X30, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X27, REG_X28, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X25, REG_X26, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X23, REG_X24, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X21, REG_X22, REG_XSP, -16);
    host_arm64_STP_PREIDX_X(block, REG_X19, REG_X20, REG_XSP, -64);

    host_arm64_MOVX_IMM(block, REG_CPUSTATE, (uint64_t) &cpu_state);

    if (block->flags & CODEBLOCK_HAS_FPU) {
        host_arm64_LDR_IMM_W(block, REG_TEMP, REG_CPUSTATE, ... &cpu_state.TOP ...);
        host_arm64_SUB_IMM(block, REG_TEMP, REG_TEMP, block->TOP);
        host_arm64_STR_IMM_W(block, REG_TEMP, REG_XSP, IREG_TOP_diff_stack_offset);
    }
}
```

**Analysis**:

1. **Register saves**: 6 `STP` instructions save X19-X30 (12 registers). The first STP uses SP-relative pre-index with -16, but the last one uses -64 to create a 96-byte frame in one shot. Actually, looking more carefully: the `STP_PREIDX` with -64 on the last one creates the full stack frame.

   Wait -- the STP order is reversed. The first STP pushes X29/X30 (highest addresses), then X27/X28, etc. Each one decrements SP by 16. So the total decrement is 6 * 16 = 96 bytes... except the last one decrements by 64, not 16. This means the stack layout has a gap.

   Actually, re-reading: `STP_PREIDX_X(block, REG_X19, REG_X20, REG_XSP, -64)` means SP is decremented by 64 before storing. The epilogue uses `LDP_POSTIDX_X(block, REG_X19, REG_X20, REG_XSP, 64)`, incrementing SP by 64 after loading. This creates a 64-byte gap at the bottom of the stack frame. The total stack frame is 16+16+16+16+16+64 = 144 bytes. This is intentional -- the gap is used for local variables (e.g., `IREG_TOP_diff_stack_offset`).

2. **cpu_state pointer**: `MOVX_IMM` loads the 64-bit address of `cpu_state` into X29. This costs 1-4 instructions depending on the address. Since `cpu_state` is a global, it will always be a full 64-bit address on macOS/Linux, costing 3-4 instructions.

3. **FPU TOP setup**: Conditional on `CODEBLOCK_HAS_FPU`. Loads current TOP from cpu_state, subtracts the block's assumed TOP, stores the difference on the stack. This is 3 instructions.

**Total prologue cost**: 6 (STP) + 3-4 (MOVX_IMM) + 0-3 (FPU) = 9-13 instructions.

### 6.2 Epilogue

**File**: `codegen_backend_arm64.c`, lines 367-378

```c
void
codegen_backend_epilogue(codeblock_t *block)
{
    host_arm64_LDP_POSTIDX_X(block, REG_X19, REG_X20, REG_XSP, 64);
    host_arm64_LDP_POSTIDX_X(block, REG_X21, REG_X22, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X23, REG_X24, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X25, REG_X26, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X27, REG_X28, REG_XSP, 16);
    host_arm64_LDP_POSTIDX_X(block, REG_X29, REG_X30, REG_XSP, 16);
    host_arm64_RET(block, REG_X30);
    ...
}
```

**Total epilogue cost**: 6 (LDP) + 1 (RET) = 7 instructions.

### 6.3 Exit Routine

**File**: `codegen_backend_arm64.c`, lines 317-324

```c
codegen_exit_rout = &block_write_data[block_pos];
host_arm64_LDP_POSTIDX_X(block, REG_X19, REG_X20, REG_XSP, 64);
host_arm64_LDP_POSTIDX_X(block, REG_X21, REG_X22, REG_XSP, 16);
// ... same as epilogue ...
host_arm64_RET(block, REG_X30);
```

The exit routine is a shared copy of the epilogue, jumped to on error conditions (abort, GPF). This avoids duplicating the epilogue in every error path.

### 6.4 V8-V15 Not Saved

The AAPCS64 calling convention requires callee-saved V8-V15 (lower 64 bits). The prologue does not save these, and the register allocator uses V8-V15 for FP/MMX host registers:

```c
host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS] = {
    { REG_V8,  0}, { REG_V9,  0}, { REG_V10, 0}, { REG_V11, 0},
    { REG_V12, 0}, { REG_V13, 0}, { REG_V14, 0}, { REG_V15, 0}
};
```

**This means any NEON code that modifies V8-V15 will corrupt the caller's SIMD state.** This is a latent correctness issue -- it only manifests if the caller (the emulator's main loop) has live values in V8-V15 when calling into JIT code.

**Recommendation**: Add STP/LDP for used V8-V15 registers in the prologue/epilogue, at least for blocks that use FP/MMX registers.

### 6.5 REG_CPUSTATE = X29 (Frame Pointer Repurposed)

X29 is the platform frame pointer register. Using it as the cpu_state base pointer means:
- Debuggers/profilers cannot unwind through JIT code
- Platform tools (Instruments, ASAN) will not work correctly in JIT frames

This is a common tradeoff in JIT compilers. The benefit is that X29 is callee-saved, so the cpu_state pointer survives across calls without needing to be reloaded.

---

## 7. Comparison with box64/FEX-Emu

### 7.1 86Box vs box64

**box64** is a Linux-only x86_64 emulator with an ARM64 dynarec. Key differences:

| Feature | 86Box | box64 |
|---------|-------|-------|
| **Target** | Full PC emulation (486-P6 era) | x86_64 application-level |
| **JIT approach** | 3-stage (decode -> UOP IR -> ARM64) | 2-stage (decode -> ARM64, some IR) |
| **Register mapping** | Dynamic allocation (X19-X28) | Static mapping (x86 regs -> ARM64 regs) |
| **Memory model** | TLB fast path + C helper fallback | mmap-based, some inline TLB |
| **Flag handling** | Lazy (flag accumulation) | Lazy + deferred |
| **I-cache** | `__clear_cache` per block | `__clear_cache` per block |
| **NEON** | MMX/3DNow! via V8B/V4H/V2S | SSE/SSE2/SSE3 via full 128-bit V |
| **W^X** | MAP_JIT (Apple), RWX (Linux) | mprotect toggle (Linux) |
| **Code quality** | Good for target scope | More optimized (larger scope) |

box64's advantages:
- **Static register mapping** avoids spill/fill overhead for frequently-accessed x86 registers
- **More aggressive immediate optimization** including MOVN and ADRP+ADD
- **CBZ/CBNZ/TBZ/TBNZ** used extensively for flag tests
- **LDP/STP pairing** for context save/restore

86Box's advantages:
- **3-stage pipeline** allows IR-level optimizations that are harder in box64's more direct translation
- **Simpler memory model** -- 86Box emulates a full PC with virtual memory, so the TLB approach is appropriate
- **Smaller scope** -- targeting 486-P6 era CPUs means no SSE/AVX complexity

### 7.2 86Box vs FEX-Emu

**FEX-Emu** is a high-performance x86/x86_64 emulator focused on Linux gaming. It uses a much more sophisticated backend:

| Feature | 86Box | FEX-Emu |
|---------|-------|---------|
| **IR** | Simple UOP stream | Full SSA IR with optimization passes |
| **Register allocator** | Linear scan | Graph coloring |
| **Immediate handling** | Binary search table | Computed at JIT time |
| **Branch optimization** | Always 2-instruction pattern | Near/far branch selection |
| **Call optimization** | Always MOVX_IMM+BLR | PC-relative BL where possible |
| **Memory ops** | Helper call per access | Inline TLB with ADRP |
| **NEON** | MMX/3DNow! only | Full SSE4.2, AVX (partial) |
| **W^X** | MAP_JIT | `pthread_jit_write_protect_np` + `mprotect` |
| **Code cache** | Fixed-size blocks | Variable-size with compaction |

FEX-Emu is significantly more sophisticated, but also targets a much harder problem (running modern x86_64 games). Many of its optimizations are overkill for 86Box's 486-P6 scope.

### 7.3 Recommendations Informed by box64/FEX

From analyzing these projects, the most impactful improvements for 86Box would be:

1. **Fix the PFRSQRT bug** (section 2.1) -- critical correctness issue
2. **MOVN optimization** in MOVX_IMM -- reduces call overhead by 1-2 instructions
3. **ADRP+ADD for nearby addresses** -- 2 instructions vs 3-4 for MOVX_IMM
4. **Near branch variant** -- single B.cond for intra-block jumps
5. **Save V8-V15 in prologue** -- correctness fix for SIMD register convention
6. **Add pthread_jit_write_protect_np** -- future-proofing for Apple Silicon
7. **Narrow I-cache invalidation** -- flush only written bytes, not full block

Lower priority but beneficial:
8. **CBZ/CBNZ for compare-and-branch-zero** -- already has emitter, could be used more
9. **TBZ/TBNZ for single-bit tests** -- has TBNZ emitter, could be used for flag tests
10. **LDP/STP pairing** for adjacent cpu_state loads/stores
11. **Integer MUL/IMUL UOPs** -- avoid C helper call overhead
12. **VRECPE/VRSQRTE** for 3DNow! reciprocal/rsqrt

---

## Summary of Findings

### Critical Bugs

| # | Issue | Severity | Location |
|---|-------|----------|----------|
| 1 | PFRSQRT overwrites FSQRT result | **High** | uops.c:1879 |
| 2 | V8-V15 callee-saved regs not preserved | **Medium** | backend.c prologue |

### Code Quality Issues

| # | Issue | Impact | Location |
|---|-------|--------|----------|
| 3 | Conditional branch always 2 instructions | Performance | ops.c:468-578 |
| 4 | MOVX_IMM never uses MOVN | Performance | ops.c:1071-1080 |
| 5 | Call sequence always 3-5 instructions | Performance | ops.c:1518-1522 |
| 6 | No ADRP+ADD for nearby addresses | Performance | ops.c throughout |
| 7 | I-cache flush per full block | Performance | allocator.c:195 |
| 8 | No pthread_jit_write_protect_np | Compatibility | unix.c:437-438 |
| 9 | PFRCP/PFRSQRT use FDIV instead of VRECPE | Performance | uops.c:1858-1882 |
| 10 | No LDP/STP pairing | Performance | uops.c throughout |
| 11 | LOAD_FUNC_ARG 1-3 unimplemented | Completeness | uops.c:832-848 |
| 12 | FP rounding via indirect jump table | Performance | backend.c:240-281 |

### Positive Observations

1. **Excellent NEON coverage** for MMX and 3DNow! -- all operations are natively vectorized
2. **Correct logical immediate encoding** via binary search table
3. **Good barrel shifter use** -- `ADD_LSHIFT` UOP directly exploits ARM64 shifted operands
4. **Smart ADD/SUB immediate handling** -- auto-negation, two-step for 24-bit values
5. **Clean sub-register handling** -- BFI-based merge pattern is correct and efficient
6. **Shared exit routine** -- single copy of epilogue, branched to from all error paths
7. **TLB fast path** in load/store routines is well-structured with inline fallback
8. **CSEL usage** for FCOM/FTST is idiomatic ARM64 (avoids conditional branches)
