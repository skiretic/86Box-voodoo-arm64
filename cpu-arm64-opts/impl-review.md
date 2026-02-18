# ARM64 CPU JIT Backend Optimization — Implementation Review

**Date**: 2026-02-17
**Branch**: `86box-arm64-cpu`
**Commits reviewed**: `561170bde` through `ed74acd61` (5 commits)

---

## Summary

Phases 1 and 2 are implemented and compile successfully. The overall design is sound, the opcode encodings are verified correct, and the Phase 2 BL optimization correctly handles the critical `codegen_alloc` hazard. However, **one correctness bug was found in Phase 1** (dest==src register aliasing in PFRCP and PFRSQRT), and several minor issues and improvements are documented below.

---

## Phase 1 Assessment

### Opcode Encodings: ALL CORRECT

Verified mathematically against the ARM Architecture Reference Manual:

| Opcode | Stored | Match |
|--------|--------|-------|
| `OPCODE_FRECPE_V2S` | `0x0ea1d800` | CORRECT |
| `OPCODE_FRSQRTE_V2S` | `0x2ea1d800` | CORRECT |
| `OPCODE_FRECPS_V2S` | `0x0e20fc00` | CORRECT |
| `OPCODE_FRSQRTS_V2S` | `0x0ea0fc00` | CORRECT |

### Newton-Raphson Math: CORRECT

Both PFRCP and PFRSQRT sequences implement the standard Newton-Raphson refinement correctly:
- PFRCP: `x1 = x0 * (2 - x0*a)` via FRECPE + FRECPS + FMUL
- PFRSQRT: `x1 = x0 * (3 - x0^2 * a) / 2` via FRSQRTE + FMUL + FRSQRTS + FMUL

### CRITICAL BUG: Dest==Src Register Aliasing (HIGH SEVERITY)

**Affected**: `codegen_PFRCP` and `codegen_PFRSQRT` in `src/codegen_new/codegen_backend_arm64_uops.c`

When a 3DNow! instruction uses the same register for source and destination (e.g., `PFRCP mm0, mm0`), the code at lines 1863-1866 breaks:

```c
// Current PFRCP (BROKEN when dest_reg == src_reg_a):
host_arm64_FRECPE_V2S(block, dest_reg, src_reg_a);              // dest = ~1/src
// src_reg_a is NOW CLOBBERED (it IS dest_reg!)
host_arm64_FRECPS_V2S(block, REG_V_TEMP, dest_reg, src_reg_a);  // reads clobbered src!
// Computes: 2 - dest*dest (WRONG) instead of 2 - dest*original_src
host_arm64_FMUL_V2S(block, dest_reg, dest_reg, REG_V_TEMP);     // wrong refinement
```

The same issue affects PFRSQRT at lines 1887-1891 where `src_reg_a` is read at the FRSQRTS instruction after being clobbered by the initial FRSQRTE.

**Evidence**: The IR at `src/codegen_new/codegen_ops_3dnow.c` line 157 generates `uop_PFRCP(ir, IREG_MM(dest_reg), IREG_MM(src_reg))` where `dest_reg` and `src_reg` can be the same value (both extracted from the ModRM byte). `PFRCP mm0, mm0` is a common pattern in real 3DNow! code.

**The x86-64 backend handles this correctly** (line 1938 of `codegen_backend_x86-64_uops.c`) by saving the source to a temp register before writing the destination.

**Recommended fix** — reorder to place the estimate into `REG_V_TEMP` first:

For PFRCP:
```c
host_arm64_FRECPE_V2S(block, REG_V_TEMP, src_reg_a);           // temp = ~1/src (src preserved)
host_arm64_FRECPS_V2S(block, dest_reg, REG_V_TEMP, src_reg_a); // dest = 2 - temp*src
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, dest_reg);    // dest = temp * dest
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

For PFRSQRT:
```c
host_arm64_FRSQRTE_V2S(block, REG_V_TEMP, src_reg_a);          // temp = ~1/sqrt(src) (src preserved)
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, REG_V_TEMP);  // dest = temp^2
host_arm64_FRSQRTS_V2S(block, dest_reg, dest_reg, src_reg_a);  // dest = (3 - dest*src) / 2
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, dest_reg);    // dest = temp * step
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

This is safe because `src_reg_a` is not written until after the last read. The math is identical (verified).

---

## Phase 2 Assessment: CORRECT AND COMPLETE

### `host_arm64_call_intrapool`: Verified correct

- `codegen_alloc(block, 4)` is correctly placed BEFORE the PC offset capture (line 1555)
- `OPCODE_BL = 0x94000000` is correct (bit 31=1 distinguishes from B)
- `OFFSET26` and `offset_is_26bit` are correct
- Pattern matches the existing `host_arm64_B` function exactly

### Call site migration: 26/26 verified

All 26 intra-pool stub calls were converted. All 6 external function calls (CALL_FUNC, CALL_FUNC_RESULT, CALL_INSTRUCTION_FUNC, FP_ENTER, MMX_ENTER, LOAD_SEG) were correctly left as `host_arm64_call`.

### JMP optimization: Verified correct

`codegen_JMP` now calls `host_arm64_B(block, uop->p)` directly. All `uop_JMP` call sites (verified in `codegen_ops_branch.c`) pass `codegen_exit_rout` which is always intra-pool.

### Minor issues

1. **Dead code**: `host_arm64_jump` at lines 1564-1568 of `src/codegen_new/codegen_backend_arm64_ops.c` has zero callers and should be removed.
2. The plan mentions `codegen_gpf_rout` as a BL target, but it is only reached via CBNZ branches, not direct calls. Minor doc inaccuracy.

---

## Correctness Issue #2: `imm_data` Type in Phase 3 Plan (MEDIUM)

The Phase 3 plan claims `uop->imm_data` is `uint32_t`. On ARM64, it is actually `uintptr_t` (64-bit), defined at line 340 of `src/codegen_new/codegen_ir_defs.h`:

```c
#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM || defined __aarch64__ || defined _M_ARM64
    uintptr_t     imm_data;
```

Phase 3 should add a debug assertion and explicit `(uint32_t)` cast. In practice all callers store values that fit in 32 bits, so this is safe but the plan text is incorrect.

---

## Recommendations for Phases 3-5

### Recommended phase reorder: Phase 3 → Phase 5 → Phase 4 (selective)

- **Phase 3** (LOAD_FUNC_ARG_IMM width fix): Trivial, 4 line changes, near-zero risk. Do first.
- **Phase 5** (LIKELY/UNLIKELY, prefetch, branchless validation): Very low risk, measurable impact especially on in-order ARM64 cores (Cortex-A53). Do second.
- **Phase 4** (new emitters): The plan does NOT identify concrete UOP handlers that would use the proposed CSEL variants, ADDS/SUBS, or CLZ emitters. Without consumers, these are dead code. Investigate whether `UOP_BSR`/`UOP_BSF`/`UOP_LZCNT` exist in the IR before implementing CLZ. Defer CSEL and ADDS/SUBS unless specific consumers are identified.

### Additional Phase 5 recommendations

1. Make branchless block validation non-optional (clear win on in-order cores)
2. Guard `__builtin_prefetch` with `#if defined(__aarch64__) || defined(_M_ARM64)` to avoid hash computation overhead on x86
3. Consider including the R6 exception dispatch noinline refactor (moves cold SMI/NMI/IRQ code out of the hot loop I-cache footprint)

---

## Summary of Action Items

| # | Priority | Description |
|---|----------|-------------|
| 1 | **P0** | Fix dest==src aliasing in PFRCP and PFRSQRT (reorder to write estimate to REG_V_TEMP first) |
| 2 | P1 | Remove dead `host_arm64_jump` function |
| 3 | P1 | Update Phase 3 plan: `imm_data` is `uintptr_t` on ARM64, add debug assertion |
| 4 | P2 | Reorder phases: move Phase 5 before Phase 4 |
| 5 | P2 | Identify concrete UOP consumers before implementing Phase 4 emitters |
| 6 | P3 | Add blank lines between new emitter functions (style) |
| 7 | P3 | Fix `codegen_gpf_rout` mention in plan (not a BL target) |
