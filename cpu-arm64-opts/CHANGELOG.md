# ARM64 CPU JIT Backend Optimization — Changelog

## Phase 1: PFRSQRT Bug Fix + 3DNow! Estimates

**Branch**: `86box-arm64-cpu`
**Commit**: `53e5658b2`
**Files changed**: 3 (+46, -7)

### Bug Fix: PFRSQRT Register Clobber (Critical)

`codegen_PFRSQRT` in `codegen_backend_arm64_uops.c` had a register clobber
bug that produced incorrect results for all 3DNow! PFRSQRT operations on
emulated AMD K6-2/K6-III processors.

**Root cause**: `FMOV_S_ONE` overwrote `REG_V_TEMP` which already held the
sqrt result from the preceding `FSQRT_S`. The subsequent `FDIV_S` then
computed `dest / 1.0` (identity) instead of `1.0 / sqrt(src)`.

```c
// BEFORE (broken):
host_arm64_FSQRT_S(block, REG_V_TEMP, src_reg_a);   // V_TEMP = sqrt(src)
host_arm64_FMOV_S_ONE(block, REG_V_TEMP);            // CLOBBERS sqrt result!
host_arm64_FDIV_S(block, dest_reg, dest_reg, REG_V_TEMP); // dest / 1.0
```

This bug is eliminated by the full rewrite below.

### New Opcodes

Added to `codegen_backend_arm64_ops.c`:

| Opcode | Encoding | Description |
|--------|----------|-------------|
| `OPCODE_FRECPE_V2S` | `0x0ea1d800` | Floating-point reciprocal estimate (2×f32) |
| `OPCODE_FRECPS_V2S` | `0x0e20fc00` | Floating-point reciprocal step / Newton-Raphson (2×f32) |
| `OPCODE_FRSQRTE_V2S` | `0x2ea1d800` | Floating-point reciprocal sqrt estimate (2×f32) |
| `OPCODE_FRSQRTS_V2S` | `0x0ea0fc00` | Floating-point reciprocal sqrt step / Newton-Raphson (2×f32) |

`OPCODE_FMUL_V2S` (`0x2e20dc00`) already existed — no duplicate added.

### New Emitters

Added to `codegen_backend_arm64_ops.c` + declared in `codegen_backend_arm64_ops.h`:

- `host_arm64_FRECPE_V2S(block, dst, src)` — single-operand estimate
- `host_arm64_FRECPS_V2S(block, dst, src_n, src_m)` — two-operand NR step
- `host_arm64_FRSQRTE_V2S(block, dst, src)` — single-operand estimate
- `host_arm64_FRSQRTS_V2S(block, dst, src_n, src_m)` — two-operand NR step

### Rewritten Handlers

**codegen_PFRCP** (AMD 3DNow! reciprocal estimate):

```
FRECPE  dest, src           // ~8-12 bit estimate
FRECPS  temp, dest, src     // Newton-Raphson: step = 2 - dest*src
FMUL    dest, dest, temp    // dest *= step → ~16-24 bit precision
DUP     dest, dest[0]       // broadcast lane 0 (3DNow! scalar semantic)
```

Was: `FMOV_S_ONE + FDIV_S` (10-30 cycle latency). Now: ~6 cycles.

**codegen_PFRSQRT** (AMD 3DNow! reciprocal sqrt estimate):

```
FRSQRTE dest, src           // ~8-12 bit estimate
FMUL    temp, dest, dest    // temp = dest²
FRSQRTS temp, temp, src     // Newton-Raphson: step = (3 - temp*src) / 2
FMUL    dest, dest, temp    // dest *= step → ~16-24 bit precision
DUP     dest, dest[0]       // broadcast lane 0 (3DNow! scalar semantic)
```

Was: `FSQRT_S + FMOV_S_ONE + FDIV_S` (21-65 cycle latency, **plus wrong
results** due to clobber bug). Now: ~6 cycles + correct.

### Why Newton-Raphson is Mandatory

ARM only guarantees 8-bit mantissa precision for FRECPE/FRSQRTE (implementation-
defined, varies from ~8 bits on Cortex-A53 to ~12 bits on Apple Silicon). AMD
3DNow! specifies PFRCP at 14-bit and PFRSQRT at 15-bit precision. One Newton-
Raphson refinement step doubles the precision to 16-24 bits, safely exceeding
the AMD spec on all ARMv8.0 implementations.

### Performance Impact

| Operation | Before (cycles) | After (cycles) | Speedup | Correctness |
|-----------|----------------|----------------|---------|-------------|
| PFRCP | 10-30 (FDIV) | ~6 | 2-5x | Was correct |
| PFRSQRT | 21-65 (FSQRT+FDIV) | ~6 | 4-10x | Was **broken** |

### ISA Requirement

All instructions are **ARMv8.0-A baseline**. No change to minimum spec.

---

## Phase 2: PC-Relative BL for Intra-Pool Stub Calls

**Branch**: `86box-arm64-cpu`
**Commit**: `32ecb6448`
**Files changed**: 3 (+42, -27)

### Problem

Every call to a JIT memory stub (load/store helpers, FP rounding, GPF handler)
used `host_arm64_call()`, which materializes a 64-bit absolute address via
MOVZ+MOVK chain (1-4 instructions) then BLR (1 instruction) = 3-5 instructions
per call site. Since all stubs live in the same ~120MB JIT memory pool, they are
always within BL's +/-128MB PC-relative range.

### Solution

Added `host_arm64_call_intrapool()` which emits a single **BL** instruction
(opcode `0x94000000`) with a 26-bit PC-relative offset. Also replaced
`host_arm64_jump()` (MOVX_IMM+BR) with `host_arm64_B()` (single B instruction)
in `codegen_JMP`, since its only target (`codegen_exit_rout`) is always
intra-pool.

### New Opcode

| Opcode | Encoding | Description |
|--------|----------|-------------|
| `OPCODE_BL` | `0x94000000` | Branch with Link, PC-relative +/-128MB |

### New Function

`host_arm64_call_intrapool(codeblock_t *block, void *dest)` -- emits a single
BL instruction. **Critical safety measure**: calls `codegen_alloc(block, 4)`
before capturing the PC offset to prevent SIGBUS from block reallocation
changing `block_write_data` between offset capture and instruction emit.

### Call Sites Modified

**26 intra-pool stub calls** replaced in `codegen_backend_arm64_uops.c`:

| Stub | Call sites | Handlers |
|------|-----------|----------|
| `codegen_mem_load_byte` | 2 | MEM_LOAD_ABS, MEM_LOAD_REG |
| `codegen_mem_load_word` | 2 | MEM_LOAD_ABS, MEM_LOAD_REG |
| `codegen_mem_load_long` | 2 | MEM_LOAD_ABS, MEM_LOAD_REG |
| `codegen_mem_load_quad` | 1 | MEM_LOAD_REG |
| `codegen_mem_load_single` | 1 | MEM_LOAD_SINGLE |
| `codegen_mem_load_double` | 1 | MEM_LOAD_DOUBLE |
| `codegen_mem_store_byte` | 5 | MEM_STORE_ABS, MEM_STORE_REG, MEM_STORE_IMM_8 |
| `codegen_mem_store_word` | 3 | MEM_STORE_ABS, MEM_STORE_REG, MEM_STORE_IMM_16 |
| `codegen_mem_store_long` | 3 | MEM_STORE_ABS, MEM_STORE_REG, MEM_STORE_IMM_32 |
| `codegen_mem_store_quad` | 1 | MEM_STORE_REG |
| `codegen_mem_store_single` | 1 | MEM_STORE_SINGLE |
| `codegen_mem_store_double` | 1 | MEM_STORE_DOUBLE |
| `codegen_fp_round` | 2 | MOV_INT_DOUBLE |
| `codegen_fp_round_quad` | 1 | MOV_INT_DOUBLE_64 |

**1 jump site** replaced: `codegen_JMP` now uses `host_arm64_B` instead of
`host_arm64_jump`.

**6 external C function calls** left unchanged (require absolute addressing):
`CALL_FUNC`, `CALL_FUNC_RESULT`, `CALL_INSTRUCTION_FUNC` (`uop->p`),
`FP_ENTER` / `MMX_ENTER` (`x86_int`), `LOAD_SEG` (`loadseg`).

### Performance Impact

| Pattern | Before (insns) | After (insns) | Savings |
|---------|---------------|---------------|---------|
| Stub call | 3-5 (MOVX_IMM+BLR) | 1 (BL) | 2-4 per call |
| JMP to exit | 4-5 (MOVX_IMM+BR) | 1 (B) | 3-4 per jump |

For a typical recompiled block with 3 memory ops + 1 JMP:
- **Before**: 3x4 + 1x4 = 16 overhead instructions
- **After**: 3x1 + 1x1 = 4 overhead instructions
- **Savings**: ~12 instructions (~48 bytes) per block

Code size reduction improves I-cache utilization and reduces block chaining
frequency (fewer blocks overflow 960-byte allocation).

### ISA Requirement

All instructions are **ARMv8.0-A baseline**. No change to minimum spec.
