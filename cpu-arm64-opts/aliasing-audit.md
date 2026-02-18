# ARM64 CPU JIT Backend: 3DNow!/MMX Register Aliasing Audit

**Date**: 2026-02-17
**Auditor**: Claude Opus 4.6
**Branch**: `86box-arm64-cpu`
**Files audited**:
- `src/codegen_new/codegen_backend_arm64_uops.c` (ARM64 backend)
- `src/codegen_new/codegen_backend_x86-64_uops.c` (x86-64 backend, for comparison)
- `src/codegen_new/codegen_ops_3dnow.c` (IR generation)
- `src/codegen_new/codegen_ir.c` / `codegen_ir_defs.h` / `codegen_reg.c` (register allocator)

---

## Background

The "dest==src aliasing bug" occurs when a multi-instruction JIT sequence writes
to the destination register before finishing all reads from the source register,
and the register allocator has assigned the same physical host register to both.

### Why dest==src Happens

The register allocator in `codegen_ir.c` (line 130-149) processes source reads
**before** destination writes. For a unary operation like `PFRCP mm0, mm0`, the
IR is `uop_PFRCP(ir, IREG_MM(0), IREG_MM(0))`. The write allocator in
`codegen_reg.c` (line 717-726) searches for the **previous version** of the same
IR register and reuses the same physical host register slot. This means
`dest_reg_a_real` and `src_reg_a_real` will have the same `HOST_REG_GET()` value.

For binary operations like `PFADD mm0, mm1`, the IR is
`uop_PFADD(ir, IREG_MM(0), IREG_MM(0), IREG_MM(1))` -- note that `dest` and
`src_a` are always the same IR register (the x86 instruction destination). The
register allocator again reuses the same physical register for dest and src_a.

### x86-64 Backend Design

The x86-64 backend was designed around SSE's destructive two-operand form, where
most instructions are `dest = dest OP src`. The x86-64 backend **requires**
`dest == src_a` for binary ops (enforced by the check
`uop->dest_reg_a_real == uop->src_reg_a_real`) and only reads `src_reg_b`. This
is safe because SSE instructions naturally handle the "dest is also first
operand" pattern.

For unary ops like PFRCP/PFRSQRT, the x86-64 backend explicitly copies
`src_reg_a` to `REG_XMM_TEMP` before writing `dest_reg`, which correctly handles
the case where dest==src.

### ARM64 Backend Design Difference

The ARM64 backend uses NEON three-operand form: `dest = src_a OP src_b`. This
means the ARM64 backend reads `src_reg_a` and `src_reg_b` separately from
`dest_reg`. For **single-instruction** operations, dest==src is safe because the
hardware reads all sources before writing the destination. But for
**multi-instruction** sequences where the first instruction writes to `dest_reg`
and a later instruction reads `src_reg_a` (which is the same register), the
source has been clobbered.

---

## Categorization of Risk

**SAFE**: Operations that emit a single NEON instruction. The ARM64 hardware
atomically reads source operands and writes the destination within one
instruction, so dest==src never causes data corruption.

**SAFE (multi-insn, uses temp)**: Operations that emit multiple instructions but
use `REG_V_TEMP` as an intermediary, avoiding writing `dest_reg` until all source
reads are complete.

**BUGGY**: Operations that emit multiple instructions, write to `dest_reg` in an
early instruction, and read `src_reg_a` (which is aliased to `dest_reg`) in a
later instruction.

---

## Summary Table

| Instruction | ARM64 Handler | # NEON Insns | dest==src_a Safe? | dest==src_b Safe? | Status |
|-------------|--------------|-------------|-------------------|-------------------|--------|
| **PFADD** | `FADD_V2S` | 1 | SAFE | SAFE | OK |
| **PFSUB** | `FSUB_V2S` | 1 | SAFE | SAFE | OK |
| **PFMUL** | `FMUL_V2S` | 1 | SAFE | SAFE | OK |
| **PFCMPEQ** | `FCMEQ_V2S` | 1 | SAFE | SAFE | OK |
| **PFCMPGE** | `FCMGE_V2S` | 1 | SAFE | SAFE | OK |
| **PFCMPGT** | `FCMGT_V2S` | 1 | SAFE | SAFE | OK |
| **PFMAX** | `FMAX_V2S` | 1 | SAFE | SAFE | OK |
| **PFMIN** | `FMIN_V2S` | 1 | SAFE | SAFE | OK |
| **PF2ID** | `FCVTZS_V2S` | 1 | SAFE | N/A (unary) | OK |
| **PI2FD** | `SCVTF_V2S` | 1 | SAFE | N/A (unary) | OK |
| **PFRCP** | `FRECPE`+`FRECPS`+`FMUL`+`DUP` | 4 | **BUGGY** | N/A (unary) | **FIX NEEDED** |
| **PFRSQRT** | `FRSQRTE`+`FMUL`+`FRSQRTS`+`FMUL`+`DUP` | 5 | **BUGGY** | N/A (unary) | **FIX NEEDED** |

### Also Audited (MMX ops, not strictly 3DNow! but relevant)

| Instruction | ARM64 Handler | # NEON Insns | dest==src_a Safe? | dest==src_b Safe? | Status |
|-------------|--------------|-------------|-------------------|-------------------|--------|
| **PMULHW** | `SMULL_V4S_4H`+`SHRN_V4H_4S` | 2 | See analysis | See analysis | **NEEDS REVIEW** |
| **PMADDWD** | `SMULL_V4S_4H`+`ADDP_V4S` | 2 | SAFE (uses temp) | SAFE (uses temp) | OK |
| **PMULLW** | `MUL_V4H` | 1 | SAFE | SAFE | OK |
| **PADDB/W/D** | `ADD_V*` | 1 | SAFE | SAFE | OK |
| **PSUBB/W/D** | `SUB_V*` | 1 | SAFE | SAFE | OK |
| **PADDS/US** | `SQADD/UQADD_V*` | 1 | SAFE | SAFE | OK |
| **PSUBS/US** | `SQSUB/UQSUB_V*` | 1 | SAFE | SAFE | OK |
| **PCMPEQ/GT** | `CMEQ/CMGT_V*` | 1 | SAFE | SAFE | OK |
| **PUNPCK*L/H** | `ZIP1/ZIP2_V*` | 1 | SAFE | SAFE | OK |
| **PACKSSWB** | `INS_D`+`INS_D`+`SQXTN` | 3 | See analysis | See analysis | OK (note) |
| **PACKSSDW** | `INS_D`+`INS_D`+`SQXTN` | 3 | See analysis | See analysis | OK (note) |
| **PACKUSWB** | `INS_D`+`INS_D`+`SQXTUN` | 3 | See analysis | See analysis | OK (note) |

---

## Detailed Analysis of Each 3DNow! Instruction

### PFADD (line 1732) -- SAFE

```c
host_arm64_FADD_V2S(block, dest_reg, src_reg_a, src_reg_b);
```

Single instruction. `FADD Vd.2S, Vn.2S, Vm.2S` reads both Vn and Vm atomically
before writing Vd. dest==src_a and dest==src_b are both handled by hardware.

**x86-64 comparison**: Uses `ADDPS xmm_dest, xmm_src_b` (destructive, requires
dest==src_a). ARM64 is strictly more general.

### PFSUB (line 1898) -- SAFE

```c
host_arm64_FSUB_V2S(block, dest_reg, src_reg_a, src_reg_b);
```

Single instruction. Same reasoning as PFADD.

**x86-64 comparison**: Has two code paths: destructive `SUBPS` when dest==src_a,
and a copy-to-temp path when dest!=src_a (for PFSUBR which swaps operands). The
ARM64 three-operand form handles both cases with a single instruction.

### PFMUL (line 1834) -- SAFE

```c
host_arm64_FMUL_V2S(block, dest_reg, src_reg_a, src_reg_b);
```

Single instruction. Same reasoning as PFADD.

### PFCMPEQ (line 1749) -- SAFE

```c
host_arm64_FCMEQ_V2S(block, dest_reg, src_reg_a, src_reg_b);
```

Single instruction. Same reasoning.

### PFCMPGE (line 1766) -- SAFE

```c
host_arm64_FCMGE_V2S(block, dest_reg, src_reg_a, src_reg_b);
```

Single instruction. Same reasoning.

### PFCMPGT (line 1783) -- SAFE

```c
host_arm64_FCMGT_V2S(block, dest_reg, src_reg_a, src_reg_b);
```

Single instruction. Same reasoning.

### PFMAX (line 1800) -- SAFE

```c
host_arm64_FMAX_V2S(block, dest_reg, src_reg_a, src_reg_b);
```

Single instruction. Same reasoning.

### PFMIN (line 1817) -- SAFE

```c
host_arm64_FMIN_V2S(block, dest_reg, src_reg_a, src_reg_b);
```

Single instruction. Same reasoning.

### PF2ID (line 1717) -- SAFE

```c
host_arm64_FCVTZS_V2S(block, dest_reg, src_reg_a);
```

Single instruction. `FCVTZS Vd.2S, Vn.2S` reads Vn before writing Vd. Unary
operation with only one source, so only dest==src_a aliasing is possible, which
is handled by hardware.

### PI2FD (line 1915) -- SAFE

```c
host_arm64_SCVTF_V2S(block, dest_reg, src_reg_a);
```

Single instruction. Same reasoning as PF2ID.

### PFRCP (line 1851) -- BUGGY when dest_reg == src_reg_a

Current code:
```c
host_arm64_FRECPE_V2S(block, dest_reg, src_reg_a);              // (1) dest = ~1/src
host_arm64_FRECPS_V2S(block, REG_V_TEMP, dest_reg, src_reg_a);  // (2) temp = 2 - dest*src
host_arm64_FMUL_V2S(block, dest_reg, dest_reg, REG_V_TEMP);     // (3) dest = dest * temp
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);               // (4) broadcast
```

**Trace when `dest_reg == src_reg_a` (e.g., `PFRCP mm0, mm0`)**:

Let the original value in the register be `a`.

1. `FRECPE dest, src` --> `dest = ~1/a`. But `src` IS `dest`, so the original
   value `a` is now gone. The register now contains `~1/a`.
2. `FRECPS temp, dest, src` --> reads `dest` = `~1/a` and `src` = `~1/a` (NOT
   the original `a`!). Computes `temp = 2 - (~1/a) * (~1/a)` = WRONG.
   Should compute `temp = 2 - (~1/a) * a`.
3. `FMUL dest, dest, temp` --> multiplies wrong values.

**Impact**: The Newton-Raphson refinement step uses the clobbered value instead
of the original source. The result will be incorrect -- it will NOT converge to
`1/a`. Instead it computes a meaningless value.

**Trigger conditions**: Any time `PFRCP mm_n, mm_n` is executed (same MMX
register for source and destination). Looking at `codegen_ops_3dnow.c` line 157:
`uop_PFRCP(ir, IREG_MM(dest_reg), IREG_MM(src_reg))` where `dest_reg` and
`src_reg` both come from the ModRM byte. `PFRCP mm0, mm0` (opcode `0F 0F C0 96`)
is legal and occurs in practice.

**x86-64 comparison** (line 1928-1948): The x86-64 backend explicitly saves src
to temp first:
```c
host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);  // Save src
host_x86_CVTSI2SS_XREG_REG(block, dest_reg, REG_ECX);       // dest = 1.0
host_x86_DIVSS_XREG_XREG(block, dest_reg, REG_XMM_TEMP);    // dest = 1.0/src
```
This is safe because `REG_XMM_TEMP` preserves the original source value.

### PFRSQRT (line 1873) -- BUGGY when dest_reg == src_reg_a

Current code:
```c
host_arm64_FRSQRTE_V2S(block, dest_reg, src_reg_a);              // (1) dest = ~1/sqrt(src)
host_arm64_FMUL_V2S(block, REG_V_TEMP, dest_reg, dest_reg);      // (2) temp = dest^2
host_arm64_FRSQRTS_V2S(block, REG_V_TEMP, REG_V_TEMP, src_reg_a);// (3) temp = (3 - temp*src)/2
host_arm64_FMUL_V2S(block, dest_reg, dest_reg, REG_V_TEMP);      // (4) dest = dest * temp
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);                // (5) broadcast
```

**Trace when `dest_reg == src_reg_a` (e.g., `PFRSQRT mm0, mm0`)**:

Let the original value in the register be `a`.

1. `FRSQRTE dest, src` --> `dest = ~1/sqrt(a)`. But `src` IS `dest`, so the
   original value `a` is now gone. The register now contains `~1/sqrt(a)`.
2. `FMUL temp, dest, dest` --> `temp = (~1/sqrt(a))^2 = ~1/a`. This is fine
   (only reads dest, which is valid).
3. `FRSQRTS temp, temp, src` --> reads `temp` = `~1/a` and `src` = `~1/sqrt(a)`
   (NOT the original `a`!). Computes `temp = (3 - (~1/a) * (~1/sqrt(a))) / 2`
   = WRONG. Should compute `temp = (3 - (~1/a) * a) / 2`.
4. `FMUL dest, dest, temp` --> multiplies wrong values.

**Impact**: Same as PFRCP. The refinement step uses the clobbered source value,
producing an incorrect result.

**x86-64 comparison** (line 1950-1970): Same pattern -- saves src to temp first:
```c
host_x86_SQRTSS_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);  // temp = sqrt(src)
host_x86_CVTSI2SS_XREG_REG(block, dest_reg, REG_ECX);         // dest = 1.0
host_x86_DIVSS_XREG_XREG(block, dest_reg, REG_XMM_TEMP);      // dest = 1.0/sqrt(src)
```
Safe because SQRTSS writes to `REG_XMM_TEMP`, not `dest_reg`.

---

## Verification of Proposed Fix from impl-review.md

The fix proposed in `cpu-arm64-opts/impl-review.md` reorders the instructions so
that the initial estimate goes into `REG_V_TEMP` instead of `dest_reg`,
preserving `src_reg_a` for the refinement step.

### Proposed PFRCP Fix

```c
host_arm64_FRECPE_V2S(block, REG_V_TEMP, src_reg_a);           // temp = ~1/src (src preserved)
host_arm64_FRECPS_V2S(block, dest_reg, REG_V_TEMP, src_reg_a); // dest = 2 - temp*src
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, dest_reg);    // dest = temp * dest
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

**Verification trace when `dest_reg == src_reg_a`** (register contains `a`):

1. `FRECPE temp, src` --> `temp = x0 = ~1/a`. The register aliased as
   `dest_reg`/`src_reg_a` still contains `a` (not clobbered).
2. `FRECPS dest, temp, src` --> reads `temp` = `x0` and `src` = `a`.
   Computes `dest = 2 - x0*a`. This is the correct Newton-Raphson step factor.
   Now `src_reg_a` (aliased to `dest_reg`) contains `2 - x0*a`, but we no
   longer need the original `a`.
3. `FMUL dest, temp, dest` --> `dest = x0 * (2 - x0*a) = x1`. This is the
   standard Newton-Raphson reciprocal refinement: `x1 = x0 * (2 - x0*a)`.
4. `DUP dest, dest, 0` --> broadcasts the low 32-bit lane.

**Mathematical verification**:
- Newton-Raphson for `f(x) = 1/x - a` (finding `x = 1/a`):
  - `x_{n+1} = x_n * (2 - x_n * a)`
  - Step 1: `x0 = FRECPE(a)` (initial estimate)
  - Step 2-3: `x1 = x0 * (2 - x0 * a)` (refinement)
- FRECPS computes exactly `2 - Vn*Vm`, which is the factor `(2 - x0*a)`.
- The sequence correctly implements one Newton-Raphson iteration. **VERIFIED.**

**Register safety verification**:
- `src_reg_a` is read in instructions (1) and (2).
- `src_reg_a` (aliased to `dest_reg`) is first written in instruction (2).
- After instruction (2), `src_reg_a` is never read again. **SAFE.**
- `REG_V_TEMP` is written in (1) and read in (2) and (3). No conflict. **SAFE.**

### Proposed PFRSQRT Fix

```c
host_arm64_FRSQRTE_V2S(block, REG_V_TEMP, src_reg_a);          // temp = ~1/sqrt(src) (src preserved)
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, REG_V_TEMP);  // dest = temp^2
host_arm64_FRSQRTS_V2S(block, dest_reg, dest_reg, src_reg_a);  // dest = (3 - dest*src) / 2
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, dest_reg);    // dest = temp * step
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

**Verification trace when `dest_reg == src_reg_a`** (register contains `a`):

1. `FRSQRTE temp, src` --> `temp = x0 = ~1/sqrt(a)`. The register aliased as
   `dest_reg`/`src_reg_a` still contains `a`.
2. `FMUL dest, temp, temp` --> `dest = x0^2`. Now `src_reg_a` (aliased to
   `dest_reg`) contains `x0^2`, NOT `a`. But we still need `a` in step 3!

   **WAIT** -- this is a problem. After step (2), `dest_reg` (which IS
   `src_reg_a`) has been overwritten with `x0^2`. Step (3) reads `src_reg_a`
   expecting the original `a`.

   Let me re-check... step (3) reads `dest_reg` and `src_reg_a`:
   - `dest_reg` = `x0^2` (written in step 2)
   - `src_reg_a` = `x0^2` (same register, clobbered!)
   - Computes `(3 - x0^2 * x0^2) / 2` = WRONG. Should compute `(3 - x0^2 * a) / 2`.

**CONCLUSION: The proposed PFRSQRT fix in impl-review.md is ALSO BUGGY when
dest_reg == src_reg_a.**

The issue is that step (2) writes to `dest_reg` (aliased to `src_reg_a`) before
step (3) reads `src_reg_a`.

### Correct PFRSQRT Fix

To fix PFRSQRT, we need to ensure `src_reg_a` is not written until after all
reads. One approach: use `REG_V_TEMP` for the estimate AND save the original
source. But we only have one temp register. The correct approach is to defer the
write to `dest_reg` until after all reads of `src_reg_a`:

**Option A: Two temp registers (not available -- only REG_V_TEMP exists)**

**Option B: Reorder to avoid the conflict**

```c
host_arm64_FRSQRTE_V2S(block, REG_V_TEMP, src_reg_a);          // temp = x0 = ~1/sqrt(a)
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, src_reg_a);   // dest = x0 * a
host_arm64_FRSQRTS_V2S(block, dest_reg, dest_reg, REG_V_TEMP); // dest = (3 - (x0*a)*x0) / 2
host_arm64_FMUL_V2S(block, dest_reg, dest_reg, REG_V_TEMP);    // dest = step * x0
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

Wait, let me verify the FRSQRTS semantics. According to the ARM ARM, FRSQRTS
computes `(3 - Vn*Vm) / 2`. The standard Newton-Raphson for reciprocal sqrt is:

Given estimate `x0` for `1/sqrt(a)`:
- `x1 = x0 * (3 - a * x0^2) / 2`

Breaking this down:
- Need `x0^2 * a` as the product inside FRSQRTS.
- FRSQRTS(p, q) = `(3 - p*q) / 2`
- We need `(3 - (x0^2) * a) / 2` = `FRSQRTS(x0^2, a)` or `FRSQRTS(a, x0^2)`.

The current (broken) code does:
1. `temp = x0^2` (via FMUL dest, dest)
2. `FRSQRTS(temp, temp, src_reg_a)` = `FRSQRTS(x0^2, a)` = `(3 - x0^2 * a) / 2`
   (correct formula, but src_reg_a is clobbered)

**Option B trace when `dest_reg == src_reg_a`** (register contains `a`):

1. `FRSQRTE temp, src` --> `temp = x0`. Register `dest/src` still has `a`.
2. `FMUL dest, temp, src` --> `dest = x0 * a`. Now `src_reg_a` has `x0*a`.
   We no longer have `a`, but...
3. `FRSQRTS dest, dest, temp` --> `dest = (3 - (x0*a) * x0) / 2`
   = `(3 - x0^2 * a) / 2`. This is CORRECT!
4. `FMUL dest, dest, temp` --> `dest = ((3 - x0^2*a)/2) * x0 = x1`. CORRECT!

**Option B is mathematically correct!** The trick is computing `x0*a` first
(using FMUL with temp and src) instead of `x0^2` (using FMUL with temp and temp).
Then FRSQRTS takes `(x0*a, x0)` instead of `(x0^2, a)`, which yields the same
result: `(3 - x0*a*x0) / 2 = (3 - x0^2*a) / 2`.

Let me verify register safety for Option B:
- `src_reg_a` is read in instructions (1) and (2). First written in (2). After
  (2), it is never read again. **SAFE.**
- `REG_V_TEMP` is written in (1), read in (2), (3), and (4). Never written
  after (1). **SAFE.**
- `dest_reg` is written in (2), read/written in (3), read/written in (4). No
  conflict since (3) reads `dest` from (2). **SAFE.**

**VERIFIED: Option B is the correct fix for PFRSQRT.**

---

## Audit of MMX Multi-Instruction Handlers

While the scope of this audit is 3DNow!, three MMX handlers use multi-instruction
sequences that deserve examination.

### PMULHW (line 1949) -- NEEDS REVIEW

```c
host_arm64_SMULL_V4S_4H(block, dest_reg, src_reg_a, src_reg_b);  // (1) dest = widening_mul(a, b)
host_arm64_SHRN_V4H_4S(block, dest_reg, dest_reg, 16);           // (2) dest = narrow(dest >> 16)
```

SMULL is a widening multiply: it reads the lower 64 bits of `Vn` and `Vm` (as
4x16-bit signed integers) and writes 128 bits to `Vd` (as 4x32-bit results).

**When dest_reg == src_reg_a**: Step (1) writes 128-bit `dest_reg`, clobbering
the 64-bit value in `src_reg_a`. But this is the ONLY instruction that reads
`src_reg_a`, and NEON `SMULL` reads sources before writing dest, so this is
**SAFE within step (1)**.

Step (2) only reads `dest_reg` (same register it writes), which is fine.

**When dest_reg == src_reg_b**: Same reasoning -- `SMULL` reads both sources
atomically before writing.

**However**: `SMULL` writes a **128-bit** result into `dest_reg`, but the MMX
register is only 64 bits wide (the lower 64 bits of the NEON register). After
SMULL, the upper 64 bits of `dest_reg` contain data. Then `SHRN` narrows from
128-bit to 64-bit, reading the full 128-bit input and writing only the lower
64 bits. This sequence is architecturally correct.

**Verdict**: SAFE. Both instructions handle dest==src atomically.

**BUT NOTE**: There is a subtle issue if `dest_reg == src_reg_a` AND the register
allocator expects the upper 64 bits to be preserved. SMULL writes a 128-bit
result, potentially clobbering upper bits. Since MMX registers are 64-bit, this
should be fine, but it depends on whether the allocator ever stores state in the
upper 64 bits of the NEON register. For Q-sized (64-bit MMX) registers, the upper
64 bits should be don't-care. **LOW RISK but worth noting.**

### PMADDWD (line 1931) -- SAFE

```c
host_arm64_SMULL_V4S_4H(block, REG_V_TEMP, src_reg_a, src_reg_b);  // (1) temp = widening_mul
host_arm64_ADDP_V4S(block, dest_reg, REG_V_TEMP, REG_V_TEMP);      // (2) dest = pairwise_add(temp)
```

Step (1) writes to `REG_V_TEMP`, not `dest_reg`. Step (2) reads only
`REG_V_TEMP` and writes `dest_reg`. Since `dest_reg` is never read before being
written, and sources are only read from `REG_V_TEMP`, this is safe regardless of
any aliasing between dest, src_a, and src_b. **SAFE.**

### PACKSSWB / PACKSSDW / PACKUSWB (lines 1444-1491) -- SAFE (with note)

Example (PACKSSWB):
```c
host_arm64_INS_D(block, REG_V_TEMP, dest_reg, 0, 0);      // (1) temp[0] = dest[0]
host_arm64_INS_D(block, REG_V_TEMP, src_reg_b, 1, 0);     // (2) temp[1] = src_b[0]
host_arm64_SQXTN_V8B_8H(block, dest_reg, REG_V_TEMP);     // (3) dest = narrow(temp)
```

These handlers **require** `uop->dest_reg_a_real == uop->src_reg_a_real` (checked
in the condition). So `dest_reg` IS `src_reg_a` by design. Step (1) reads
`dest_reg` (which is `src_reg_a`) and copies to `REG_V_TEMP`. Step (2) reads
`src_reg_b` and inserts into `REG_V_TEMP`. Step (3) reads `REG_V_TEMP` and
writes `dest_reg`.

Since `dest_reg` is read in step (1) before being written in step (3), and all
intermediate work goes through `REG_V_TEMP`, this is **SAFE**.

The only aliasing concern would be `dest_reg == src_reg_b`, but that would mean
the same MMX register is used as both source operands AND dest. Since the handler
reads `src_reg_b` in step (2) before writing `dest_reg` in step (3), even this
case is safe.

---

## Final Summary

### Bugs Found

| # | Instruction | Severity | Description |
|---|-------------|----------|-------------|
| 1 | **PFRCP** | HIGH | dest==src_a aliasing: FRECPE clobbers src before FRECPS reads it |
| 2 | **PFRSQRT** | HIGH | dest==src_a aliasing: FRSQRTE clobbers src before FRSQRTS reads it |

### Proposed Fix Status

| Instruction | impl-review.md Fix | Correct? |
|-------------|-------------------|----------|
| **PFRCP** | Reorder: estimate to REG_V_TEMP first | **YES -- VERIFIED CORRECT** |
| **PFRSQRT** | Reorder: estimate to REG_V_TEMP first | **NO -- STILL BUGGY** (step 2 writes dest before step 3 reads src) |

### Correct Fixes

**PFRCP** (as proposed in impl-review.md -- verified correct):
```c
host_arm64_FRECPE_V2S(block, REG_V_TEMP, src_reg_a);           // temp = x0 = ~1/src
host_arm64_FRECPS_V2S(block, dest_reg, REG_V_TEMP, src_reg_a); // dest = 2 - x0*src
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, dest_reg);    // dest = x0 * (2 - x0*src)
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);              // broadcast
```

**PFRSQRT** (corrected -- NOT the impl-review.md version):
```c
host_arm64_FRSQRTE_V2S(block, REG_V_TEMP, src_reg_a);          // temp = x0 = ~1/sqrt(src)
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, src_reg_a);   // dest = x0 * src
host_arm64_FRSQRTS_V2S(block, dest_reg, dest_reg, REG_V_TEMP); // dest = (3 - x0*src*x0) / 2
host_arm64_FMUL_V2S(block, dest_reg, dest_reg, REG_V_TEMP);    // dest = step * x0
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);              // broadcast
```

The key insight for the PFRSQRT fix is computing `x0 * a` instead of `x0 * x0`
in step 2. This consumes `src_reg_a` (the original `a`) before it gets
clobbered, while the mathematical result is identical:
- `FRSQRTS(x0*a, x0) = (3 - x0*a*x0) / 2 = (3 - x0^2*a) / 2`

### No Other Bugs Found

All other 3DNow! instructions (PFADD, PFSUB, PFMUL, PFCMPEQ, PFCMPGE, PFCMPGT,
PFMAX, PFMIN, PF2ID, PI2FD) emit a single NEON instruction and are inherently
safe with respect to register aliasing.

All audited MMX multi-instruction handlers (PMULHW, PMADDWD, PACKSSWB,
PACKSSDW, PACKUSWB) are either safe by design (single instruction or uses temp
correctly).
