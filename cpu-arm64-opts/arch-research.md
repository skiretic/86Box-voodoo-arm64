# ARM64 CPU JIT Backend -- Architecture Research Findings

**Date**: 2026-02-17
**Scope**: Phases 3-5 validation + additional optimization opportunities
**Target**: ARMv8.0-A baseline (no optional extensions)

---

## Table of Contents

1. [Phase 3: LOAD_FUNC_ARG_IMM Width Optimization](#phase-3)
2. [Phase 4: New ARM64 Emitters](#phase-4)
3. [Phase 5: __builtin_expect Analysis](#phase-5)
4. [Additional Optimization Opportunities](#additional)
5. [Final Recommendations](#recommendations)

---

<a id="phase-3"></a>
## 1. Phase 3: LOAD_FUNC_ARG_IMM Width Optimization

### 1.1 Current Implementation

The four `codegen_LOAD_FUNC_ARG*_IMM` handlers in
`src/codegen_new/codegen_backend_arm64_uops.c` (lines 851-877) currently use
`host_arm64_MOVX_IMM` (64-bit immediate load, up to 4 instructions) for values
that are always small 32-bit quantities.

```c
// Current (lines 851-853):
host_arm64_MOVX_IMM(block, REG_ARG0, uop->imm_data);  // Up to 4 insns
```

The proposed replacement is `host_arm64_mov_imm` (32-bit immediate load, 1-2
instructions):

```c
// Proposed:
host_arm64_mov_imm(block, REG_ARG0, uop->imm_data);    // 1-2 insns
```

### 1.2 imm_data Type Analysis -- Critical Finding

On ARM64, `uop->imm_data` is `uintptr_t` (64-bit), NOT `uint32_t`:

```c
// src/codegen_new/codegen_ir_defs.h lines 339-343:
#if defined __aarch64__ || defined _M_ARM64
    uintptr_t     imm_data;    // 64-bit on ARM64
#else
    uint32_t      imm_data;    // 32-bit on x86-64
#endif
```

`host_arm64_mov_imm` takes `uint32_t`:

```c
// src/codegen_new/codegen_backend_arm64_ops.h line 260:
void host_arm64_mov_imm(codeblock_t *block, int reg, uint32_t imm_data);
```

Passing `uop->imm_data` (uintptr_t) to `host_arm64_mov_imm` (uint32_t) will
silently truncate. However, this is safe because:

1. **All actual values fit in 32 bits.** The `uop_LOAD_FUNC_ARG_IMM` macro
   feeds into `uop_gen_imm()` which takes `uintptr_t` on ARM64, but the call
   sites pass values like `new_cs` (uint16_t segment selector), `op_pc + 4`
   (uint32_t program counter offset), and similar small constants.

2. **The x86-64 backend confirms this.** The x86-64 `codegen_LOAD_FUNC_ARG0_IMM`
   uses `host_x86_MOV32_REG_IMM` (32-bit MOV), demonstrating these values are
   always 32-bit quantities.

3. **W-register writes zero-extend to X.** On ARM64, writing to W0 (32-bit)
   implicitly zero-extends to X0 (64-bit). Since `REG_ARG0` = `REG_X0` = 0
   (same register number), the 32-bit MOV produces the correct 64-bit value
   for any non-negative 32-bit input.

### 1.3 host_arm64_mov_imm Edge Case Analysis

```c
// src/codegen_new/codegen_backend_arm64_ops.c lines 1571-1579:
host_arm64_mov_imm(codeblock_t *block, int reg, uint32_t imm_data)
{
    if (imm_is_imm16(imm_data))
        host_arm64_MOVZ_IMM(block, reg, imm_data);
    else {
        host_arm64_MOVZ_IMM(block, reg, imm_data & 0xffff);
        host_arm64_MOVK_IMM(block, reg, imm_data & 0xffff0000);
    }
}
```

Where `imm_is_imm16` returns true if either the upper or lower half-word is zero:

```c
static inline int imm_is_imm16(uint32_t imm_data)
{
    if (!(imm_data & 0xffff0000) || !(imm_data & 0x0000ffff))
        return 1;
    return 0;
}
```

**Edge case coverage:**

| Value | imm_is_imm16 | Result | Correct? |
|-------|-------------|--------|----------|
| `0x00000000` (zero) | true | `MOVZ W, #0` (1 insn) | Yes -- W register zeroed, X register zeroed |
| `0x00000001` | true | `MOVZ W, #1` (1 insn) | Yes |
| `0x0000FFFF` | true | `MOVZ W, #0xFFFF` (1 insn) | Yes |
| `0x00010000` | true | `MOVZ W, #1, LSL #16` (1 insn) | Yes |
| `0xFFFF0000` | true | `MOVZ W, #0xFFFF, LSL #16` (1 insn) | Yes |
| `0x00010001` | false | `MOVZ W, #1; MOVK W, #1, LSL #16` (2 insns) | Yes |
| `0xFFFFFFFF` | false | `MOVZ W, #0xFFFF; MOVK W, #0xFFFF, LSL #16` (2 insns) | Yes |

**No edge case failures.** All `uint32_t` values are handled correctly. The
function never crashes, never produces wrong results, and always emits 1-2
instructions.

### 1.4 Missing Optimization: MOVN

The function does not attempt MOVN (move with NOT) for values like `0xFFFFFFFF`
which could be encoded as `MOVN W, #0` (1 instruction instead of 2). However:

- LOAD_FUNC_ARG_IMM values are small positive integers (segment selectors,
  PC offsets, opcode constants). MOVN patterns are extremely unlikely to appear.
- Adding MOVN support would complicate the function for negligible benefit
  in this specific context.
- The broader `host_arm64_mov_imm` function could benefit from MOVN support
  for other callers (e.g., mask values like `0xFFFFFF00`), but this is a
  separate, lower-priority optimization.

### 1.5 Verdict

| Item | Status |
|------|--------|
| Correctness of truncation from uintptr_t to uint32_t | Safe -- values always fit in 32 bits |
| Zero handling | Correct -- MOVZ W, #0 zero-extends to full X register |
| Negative values | N/A -- imm_data is unsigned; bit patterns handled correctly |
| Values > 16 bits | Correct -- 2 instructions (MOVZ + MOVK) |
| Values > 32 bits | N/A for mov_imm (truncated); never occurs in practice |
| MOVN optimization gap | Real but irrelevant for this use case |
| Phase 3 risk assessment | **VERY LOW** -- confirmed safe to proceed |

---

<a id="phase-4"></a>
## 2. Phase 4: New ARM64 Emitters

### 2.1 CSEL (Conditional Select)

**ARMv8.0-A status: MANDATORY (base A64 instruction set)**

CSEL is part of the base A64 instruction set defined in the ARMv8-A Architecture
Reference Manual (DDI 0487), section C6. It is listed in the "Base Instructions"
index at developer.arm.com. Every AArch64 processor implements it -- it is not
gated behind any optional feature flag or FEAT_* extension.

Note: The Armv8.1-M specification introduced CSEL to the Thumb instruction set
for M-profile cores, which is a separate context. In the A-profile (AArch64),
CSEL has been present since ARMv8.0-A.

The existing codebase already has three CSEL variants (CSEL_CC, CSEL_EQ,
CSEL_VS) at `codegen_backend_arm64_ops.c` lines 709-721, confirming it works
on all target hardware.

**Encoding format:**

```
31 30 29 28 27 26 25 24 23 22 21 20..16 15..12 11 10 9..5 4..0
sf  0  0  1  1  0  1  0  1  0  0  Rm    cond    0  0  Rn   Rd
```

- sf=0 for 32-bit (W registers), sf=1 for 64-bit (X registers)
- Base opcode (32-bit): `0x1A800000`
- Condition field: bits [15:12], same encoding as B.cond

**Condition codes** (already defined in the codebase, lines 27-40):

| Cond | Value | Meaning |
|------|-------|---------|
| EQ | 0x0 | Equal (Z=1) |
| NE | 0x1 | Not equal (Z=0) |
| CS/HS | 0x2 | Carry set / unsigned higher or same |
| CC/LO | 0x3 | Carry clear / unsigned lower |
| MI | 0x4 | Minus / negative (N=1) |
| PL | 0x5 | Plus / positive or zero (N=0) |
| VS | 0x6 | Overflow (V=1) |
| VC | 0x7 | No overflow (V=0) |
| HI | 0x8 | Unsigned higher (C=1 and Z=0) |
| LS | 0x9 | Unsigned lower or same |
| GE | 0xA | Signed greater or equal (N=V) |
| LT | 0xB | Signed less than (N!=V) |
| GT | 0xC | Signed greater than (Z=0 and N=V) |
| LE | 0xD | Signed less or equal (Z=1 or N!=V) |

**New emitter pattern** (trivial -- identical to existing CSEL_CC/EQ/VS):

```c
void host_arm64_CSEL_NE(codeblock_t *block, int dst_reg, int src_n_reg, int src_m_reg)
{
    codegen_addlong(block, OPCODE_CSEL | CSEL_COND(COND_NE) | Rd(dst_reg) | Rn(src_n_reg) | Rm(src_m_reg));
}
```

**Related instructions (same base encoding, different op2 field):**

| Instruction | Encoding | Operation | Bit 10 |
|-------------|----------|-----------|--------|
| CSEL | `0x1A800000` | Rd = cond ? Rn : Rm | 0 |
| CSINC | `0x1A800400` | Rd = cond ? Rn : Rm+1 | 1 |
| CSINV | `0x5A800000` | Rd = cond ? Rn : ~Rm | 0 (sf=1 bit 30) |
| CSNEG | `0x5A800400` | Rd = cond ? Rn : -Rm | 1 (sf=1 bit 30) |

**Gotchas:** None. The encoding is straightforward and the existing CSEL
infrastructure proves it works. Adding more condition variants is purely
mechanical.

Sources:
- [A64 Base Instructions -- CSEL](https://developer.arm.com/documentation/dui0802/b/CSEL)
- [The AArch64 processor, part 16: Conditional execution](https://devblogs.microsoft.com/oldnewthing/20220817-00/?p=106998)
- [ARM64 Quick Reference](https://courses.cs.washington.edu/courses/cse469/19wi/arm64.pdf)

### 2.2 ADDS/SUBS (Flag-Setting Add/Subtract)

**ARMv8.0-A status: MANDATORY (base A64 instruction set)**

ADDS and SUBS are the flag-setting variants of ADD and SUB. They are base A64
instructions present in every ARMv8.0-A implementation. CMP and CMN are aliases
of SUBS and ADDS with Rd=XZR/WZR (discarding the result, keeping only flags).

The codebase already uses CMP_IMM (which IS `SUBS Rd=WZR`) and CMN_IMM (which
IS `ADDS Rd=WZR`), confirming the encoding infrastructure works:

```c
// Existing (codegen_backend_arm64_ops.c line 52-55):
#define OPCODE_CMN_IMM   (0x31 << OPCODE_SHIFT)  // = ADDS with Rd=WZR
#define OPCODE_CMP_IMM   (0x71 << OPCODE_SHIFT)  // = SUBS with Rd=WZR
```

**Encoding format (immediate):**

```
31 30 29 28..23 21..10 9..5 4..0
sf  op  S  100010 imm12  Rn   Rd
```

- sf=0 for 32-bit, sf=1 for 64-bit
- op=0 for ADD, op=1 for SUB
- S=1 for flag-setting (ADDS/SUBS), S=0 for non-flag-setting (ADD/SUB)

| Instruction | sf | op | S | Opcode bits [31:22] | Hex |
|-------------|----|----|---|---------------------|-----|
| ADD_IMM (W) | 0 | 0 | 0 | `0001_0001_00` | `0x11 << 22` |
| ADDS_IMM (W) | 0 | 0 | 1 | `0011_0001_00` | `0x31 << 22` |
| SUB_IMM (W) | 0 | 1 | 0 | `0101_0001_00` | `0x51 << 22` |
| SUBS_IMM (W) | 0 | 1 | 1 | `0111_0001_00` | `0x71 << 22` |
| CMN_IMM (W) | 0 | 0 | 1 | `0011_0001_00` | Same as ADDS_IMM |
| CMP_IMM (W) | 0 | 1 | 1 | `0111_0001_00` | Same as SUBS_IMM |

**Encoding format (shifted register):**

```
31 30 29 28..24 23..22 21 20..16 15..10 9..5 4..0
sf  op  S  01011 shift  0   Rm    imm6   Rn   Rd
```

| Instruction | Opcode bits [31:21] | Hex |
|-------------|---------------------|-----|
| ADD_LSL (W) | `0_00_01011_00_0` | `0x058 << 21` |
| ADDS_LSL (W) | `0_01_01011_00_0` | `0x158 << 21` |
| SUB_LSL (W) | `0_10_01011_00_0` | `0x258 << 21` |
| SUBS_LSL (W) | `0_11_01011_00_0` | `0x358 << 21` |
| CMP_LSL (W) | `0_11_01011_00_0` | Same as SUBS_LSL |

**New opcode defines needed:**

```c
#define OPCODE_ADDS_IMM    (0x31 << OPCODE_SHIFT)   // = CMN_IMM encoding
#define OPCODE_SUBS_IMM    (0x71 << OPCODE_SHIFT)   // = CMP_IMM encoding
#define OPCODE_ADDS_LSL    (0x158 << 21)
#define OPCODE_SUBS_LSL    (0x358 << 21)             // = CMP_LSL encoding
```

Note: ADDS_IMM and CMN_IMM share the same opcode; the difference is whether
Rd is WZR (CMN discards the result) or a real register (ADDS keeps it). Same
for SUBS_IMM and CMP_IMM.

**Use case:** Fusing `ADD Rd, Rn, #imm` + `CMP Rd, #0` into a single
`ADDS Rd, Rn, #imm` when the subsequent comparison is against zero. The
ADDS instruction sets NZCV flags based on the result, eliminating the separate
CMP.

**Gotchas:**
- The existing `host_arm64_CMP_IMM` function already validates that the
  immediate fits in 12 bits. The ADDS_IMM emitter should use the same
  validation.
- When ADDS/SUBS is used, the condition codes must be consumed before any
  subsequent flag-modifying instruction. The JIT's IR does not currently
  model flag liveness, so care is needed to ensure flags are not clobbered
  between the ADDS and the conditional branch.

Sources:
- [ARMv8 Instruction Set Overview](https://www.cs.princeton.edu/courses/archive/spr21/cos217/reading/ArmInstructionSetOverview.pdf)
- [A64 Base Instructions](https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions)

### 2.3 CLZ (Count Leading Zeros)

**ARMv8.0-A status: MANDATORY (base A64 instruction set)**

CLZ is part of the base A64 data processing instruction set. It has been
present since ARMv8.0-A and does not require any optional extension. (Note:
CLZ was also present in ARMv5T and later for A32/T32.)

**Encoding format:**

```
31 30 29..21       20..16 15..10  9..5 4..0
sf  1  0110_10110  00000  0001_00 Rn   Rd
```

| Instruction | Encoding |
|-------------|----------|
| CLZ Wd, Wn | `0x5AC01000 \| Rn(src) \| Rd(dst)` |
| CLZ Xd, Xn | `0xDAC01000 \| Rn(src) \| Rd(dst)` |

**New opcode define:**

```c
#define OPCODE_CLZ_W    (0x5AC01000)
#define OPCODE_CLZ_X    (0xDAC01000)
```

**New emitter:**

```c
void host_arm64_CLZ(codeblock_t *block, int dst_reg, int src_reg)
{
    codegen_addlong(block, OPCODE_CLZ_W | Rd(dst_reg) | Rn(src_reg));
}
```

**Use case:** x86 BSR (Bit Scan Reverse) can be implemented as:

```c
// BSR dest, src  =>  find position of highest set bit
// ARM64 equivalent:
// CLZ Wtmp, Wsrc
// MOV Wdest, #31
// SUB Wdest, Wdest, Wtmp
```

This replaces a call to a C helper function with 3 inline instructions.
BSF (Bit Scan Forward) requires RBIT + CLZ:

```c
// BSF dest, src  =>  find position of lowest set bit
// ARM64 equivalent:
// RBIT Wtmp, Wsrc
// CLZ Wdest, Wtmp
```

**Gotchas:**
- CLZ returns 32 for an input of 0 (for the 32-bit variant). x86 BSR is
  undefined for input 0, so this is safe -- the JIT never generates BSR with
  a zero input (it is guarded by a zero check).
- RBIT (Reverse Bits) is also a base ARMv8.0-A instruction, encoding
  `0x5AC00000`. It would be needed alongside CLZ for BSF emulation.

Sources:
- [CLZ -- ARM Developer](https://developer.arm.com/documentation/dui0801/k/A64-General-Instructions/CLZ)
- [A64 Base Instructions](https://www.scs.stanford.edu/~zyedidia/arm64/)

### 2.4 Phase 4 Summary

| Instruction | ARMv8.0-A | Encoding Verified | Existing Infrastructure | Risk |
|-------------|-----------|-------------------|------------------------|------|
| CSEL (more conditions) | Mandatory | Yes (existing CSEL_CC/EQ/VS) | Condition codes defined | VERY LOW |
| CSINC/CSINV/CSNEG | Mandatory | Yes (0x1A800400 / 0x5A800000 / 0x5A800400) | Same base as CSEL | LOW |
| ADDS_IMM | Mandatory | Yes (same opcode as CMN_IMM) | CMP/CMN infrastructure | LOW |
| SUBS_IMM | Mandatory | Yes (same opcode as CMP_IMM) | CMP infrastructure | LOW |
| ADDS_LSL | Mandatory | Yes (0x158 << 21) | ADD_LSL infrastructure | LOW |
| SUBS_LSL | Mandatory | Yes (same as CMP_LSL) | CMP_LSL infrastructure | LOW |
| CLZ | Mandatory | Yes (0x5AC01000) | None (new) | LOW |
| RBIT | Mandatory | Yes (0x5AC00000) | None (new, needed for BSF) | LOW |

All Phase 4 instructions are **ARMv8.0-A baseline mandatory**. None require
optional extensions, feature flags, or runtime detection. They are safe to
use unconditionally on all AArch64 processors.

---

<a id="phase-5"></a>
## 3. Phase 5: __builtin_expect (LIKELY/UNLIKELY) Analysis

### 3.1 Mechanism of Action

`__builtin_expect` does NOT emit hardware branch hints on ARM64. ARM64 has no
equivalent of x86's branch hint prefixes (0x2E/0x3E). Instead, it works
entirely through **compiler code layout**:

1. **Code placement**: The compiler places the "likely" path as the
   fall-through case and moves the "unlikely" path out-of-line. This means
   the common execution path is a contiguous sequence of instructions without
   taken branches.

2. **Block ordering**: GCC and Clang use `__builtin_expect` to assign branch
   probabilities to basic blocks. Blocks predicted as hot are placed in the
   fall-through path; cold blocks are moved to the end of the function or a
   separate `.text.unlikely` section.

3. **Optimization decisions**: The compiler may change instruction selection
   based on branch probability. For example, Clang 3.9+ switches from
   conditional moves (CSEL) to comparison-and-jump when `__builtin_expect`
   indicates a heavily skewed branch (>99% one direction).

Source: [Peeking under the hood of GCC's __builtin_expect](https://tbrindus.ca/how-builtin-expect-works/)

### 3.2 Measured Impact by Microarchitecture

**Important caveat**: Precise benchmarks of `__builtin_expect` in isolation
on ARM64 are scarce in the literature. The following estimates are derived
from branch misprediction penalty data, pipeline characteristics, and
general compiler optimization impact studies.

#### Cortex-A53 (in-order, 8-stage pipeline)

- **Branch misprediction penalty**: ~8 cycles (full pipeline flush)
- **Branch predictor**: 3072-entry global history table, simple
- **Impact of code layout**: **HIGH (5-10%)**

On an in-order core, the CPU cannot speculatively execute past an unpredicted
branch. When a taken branch occurs, the pipeline stalls while the branch
target is fetched. Placing the hot path as fall-through eliminates this stall
for the common case.

Furthermore, the A53's simple branch predictor benefits more from static
layout hints because it has fewer entries and less sophisticated prediction
algorithms. First-encounter branches (cold code) benefit the most.

Source: [ARM Cortex-A53 Architecture](https://chipsandcheese.com/2023/05/28/arms-cortex-a53-tiny-but-important/),
[7-cpu.com Cortex-A53](https://www.7-cpu.com/cpu/Cortex-A53.html)

#### Cortex-A72 (out-of-order, 15-stage pipeline)

- **Branch misprediction penalty**: ~15 cycles
- **Branch predictor**: Improved, with indirect predictor
- **Impact of code layout**: **MODERATE (3-5%)**

The OOO pipeline can hide some branch latency through speculative execution,
but the 15-cycle misprediction penalty is significant. Code layout still
matters because:
- Fall-through paths have better I-cache locality
- The fetch unit can process sequential instructions faster than taken branches
- Misprediction recovery is expensive (15 cycles of wasted work)

Source: [ARM Cortex-A72 Fetch and Branch Processing](http://sandsoftwaresound.net/arm-cortex-a72-fetch-and-branch-processing/),
[ARM Cortex-A72 Tuning](http://sandsoftwaresound.net/arm-cortex-a72-tuning-branch-mispredictions/)

#### Cortex-A76 (out-of-order, 13-stage pipeline)

- **Branch misprediction penalty**: ~11 cycles
- **Branch predictor**: Decoupled, runs ahead of fetch
- **Impact of code layout**: **MODERATE (2-4%)**

The A76's branch predictor is decoupled from the fetch pipeline and can run
ahead, reducing the impact of mispredictions compared to A72. The 11-cycle
penalty (vs 15 on A72) further reduces the benefit of static hints.

Source: [Cortex-A76 WikiChip](https://en.wikichip.org/wiki/arm_holdings/microarchitectures/cortex-a76),
[AnandTech Cortex-A76](https://www.anandtech.com/show/12785/arm-cortex-a76-cpu-unveiled-7nm-powerhouse/2)

#### Apple M-series (Firestorm, deep OOO, wide pipeline)

- **Branch misprediction penalty**: ~13-14 cycles
- **Branch predictor**: Advanced TAGE-like, very large BTB (1024+ L1 entries)
- **L1 I-cache**: ~192KB (6x larger than A53)
- **Impact of code layout**: **LOW (1-3%)**

Apple Silicon's massive I-cache and sophisticated branch predictor reduce the
benefit of static layout hints. The predictor learns quickly, and the large
I-cache means sequential code is rarely evicted. The Apple Silicon CPU
Optimization Guide specifically states that software alignment of branch
targets is unnecessary because the processor handles alignment internally.

However, code layout still matters for:
- First-encounter branches (no prediction history yet)
- Branches with extreme skew (>99.9% one direction)
- Reducing I-cache footprint (keeping hot paths together)

Source: [Firestorm Overview](https://dougallj.github.io/applecpu/firestorm.html),
[Apple Silicon CPU Optimization Guide](https://developer.apple.com/documentation/apple-silicon/cpu-optimization-guide),
[7-cpu.com Apple M1](https://www.7-cpu.com/cpu/Apple_M1.html)

### 3.3 Linux Kernel Experience

The Linux kernel uses `likely()`/`unlikely()` extensively (thousands of
annotations). Steven Rostedt's annotation profiling work revealed that a
significant number of annotations are wrong:

- `page_mapping()` had an `unlikely()` annotation that was incorrect **39%** of
  the time (1.27 billion incorrect vs 1.91 billion correct).
- Rostedt found 10 places where annotations were either unnecessary or
  outright inverted.
- The kernel has a built-in profiler (`/debugfs/tracing/profile_likely` and
  `profile_unlikely`) to validate annotations at runtime.

**Lesson**: Only annotate branches where the prediction is overwhelmingly
correct (>95%). For the CPU JIT hot loop, the candidates are well-chosen:

| Branch | Expected hit rate | Confidence |
|--------|------------------|------------|
| `cycles > 0` (loop) | >99.99% | Very high -- loop runs thousands of iterations |
| `cpu_force_interpreter` | <0.01% UNLIKELY | Very high -- JIT is normal path |
| `valid_block && WAS_RECOMPILED` | >95% LIKELY | High -- cache hit rate |
| `cpu_state.abrt` | <0.1% UNLIKELY | Very high -- aborts are exceptional |
| `smi_line` | <0.001% UNLIKELY | Very high -- SMI is extremely rare |
| `nmi && nmi_enable && nmi_mask` | <0.1% UNLIKELY | Very high -- NMI is rare |
| Page boundary cross | <0.1% UNLIKELY | High -- most fetches don't cross |
| Page cache hit | >99% LIKELY | Very high -- TLB hit rate |

All of these have extreme skew (>95% one direction), making them safe and
beneficial annotation targets.

Source: [LWN: Likely unlikely()s](https://lwn.net/Articles/420019/),
[LWN: Profile likely and unlikely annotations](https://lwn.net/Articles/305323/),
[man7.org: How much do __builtin_expect() improve performance?](http://blog.man7.org/2012/10/how-much-do-builtinexpect-likely-and.html)

### 3.4 Compilation Requirements

- `__builtin_expect` has NO effect at `-O0` or `-O1` on most compilers.
  At least `-O2` is required to see code layout changes.
- The existing `LIKELY`/`UNLIKELY` macros in `src/include/86box/86box.h`
  (lines 93-99) correctly handle non-GCC/Clang compilers with no-op fallback.
- 86Box builds with the `regular` preset (which uses `-O2` or higher on
  release builds), so the annotations will be effective.

### 3.5 Phase 5 Verdict

| Aspect | Assessment |
|--------|-----------|
| Mechanism | Code layout (no hardware hints on ARM64) |
| Impact on Cortex-A53 | **5-10%** on annotated hot paths |
| Impact on Cortex-A72/A76 | **2-5%** on annotated hot paths |
| Impact on Apple Silicon | **1-3%** on annotated hot paths |
| Annotation correctness risk | VERY LOW -- all candidates have extreme skew |
| Implementation risk | VERY LOW -- purely advisory, no behavioral change |
| Compilation requirement | `-O2` or higher (satisfied by release builds) |
| Recommendation | **Proceed** -- low effort, no risk, measurable benefit on in-order cores |

---

<a id="additional"></a>
## 4. Additional Optimization Opportunities

### 4.1 CBZ/CBNZ for Zero-Compare-and-Branch

The JIT already uses CBNZ effectively in `host_arm64_CBNZ` with a smart
fallback for out-of-range targets (line 474-486 of
`codegen_backend_arm64_ops.c`). CBZ is the inverse form.

**Current usage**: CBNZ is used for block exit checks (checking if a C function
returned non-zero to signal an abort). This is already well-optimized.

**Additional opportunity**: The interpreter loop in `386_dynarec.c` has patterns
like:

```c
if (cpu_state.abrt)
    break;
```

These compile to `LDR + CMP + B.NE`. If the abort flag were already in a
register, this could be `CBNZ Wn, target` (1 instruction instead of 3). However,
this is compiler-generated code, not JIT-emitted code, so the compiler should
already optimize this with `-O2`.

**Verdict**: No action needed in JIT code. The compiler handles this for
C-level code.

### 4.2 TBZ/TBNZ for Single-Bit Tests

TBZ (Test Bit and Branch if Zero) and TBNZ (Test Bit and Branch if Non-Zero)
test a single bit and branch. They encode the bit position directly in the
instruction, eliminating the need for a mask + compare.

**Range**: +/-32KB (14-bit signed offset), which is more limited than CBZ/CBNZ
(+/-1MB) or B.cond (+/-1MB).

**Current usage in the JIT**: The stubs already use `TST_IMM + BNE` for
single-bit flag tests. TBZ/TBNZ could replace this 2-instruction sequence with
1 instruction, but:

- The IR does not distinguish single-bit tests from general comparisons
- The bit position must be known at compile time (it is, for flag bits)
- The 32KB range limit could be problematic for some stub-to-exit distances

**V8 and .NET experience**: Both V8's TurboFan and .NET's RyuJIT ARM64
backends use TBZ/TBNZ as branch instructions. V8 marks them as branch
instructions in the instruction selector to enable proper optimization.

**Verdict**: Low priority. The IR would need to expose single-bit test
information for this to be actionable. The savings (1 instruction per
single-bit test) are modest and the range limitation adds complexity.

Source: [V8 TurboFan TBZ/TBNZ](https://groups.google.com/g/v8-dev/c/OCy_MZtLchQ),
[.NET ARM64 JIT](https://github.com/dotnet/runtime/issues/43629)

### 4.3 Conditional Compare (CCMP)

CCMP allows chaining multiple comparisons into a single condition code
evaluation without branching:

```asm
CMP W0, #5
CCMP W1, #10, #0, EQ   // Only compare W1 if previous was EQ
B.EQ target             // Branch if BOTH comparisons were equal
```

This was **already investigated and rejected** in the plan (Phase 6: Rejected
Items). The IR does not expose multi-condition patterns, so CCMP would require
an IR-level peephole optimizer. The implementation complexity is not justified
by the limited applicability.

**Verdict**: Confirmed rejected. Would require IR-level changes.

### 4.4 ADRP+ADD vs Literal Pool

ADRP+ADD generates a PC-relative address within +/-4GB. It was **already
investigated and rejected** for global variable access because macOS ASLR
makes the JIT-pool-to-global distance unpredictable.

However, ADRP+ADD IS used successfully in the prologue stubs for
intra-pool references (where the distance is known and bounded).

**Verdict**: Confirmed rejected for generated code; already used where
applicable.

### 4.5 MOVN for Immediate Loading

As analyzed in Section 1.4, `host_arm64_mov_imm` does not use MOVN to
optimize loading of values where the bitwise NOT has a simpler
representation. For example:

| Value | Current | With MOVN |
|-------|---------|-----------|
| `0xFFFFFFFF` | 2 insns (MOVZ + MOVK) | 1 insn (MOVN W, #0) |
| `0xFFFFFFFE` | 2 insns | 1 insn (MOVN W, #1) |
| `0xFFFF0001` | 2 insns | 1 insn (MOVN W, #0xFFFE) |

**Implementation** (if desired):

```c
host_arm64_mov_imm(codeblock_t *block, int reg, uint32_t imm_data)
{
    uint32_t inv = ~imm_data;
    if (imm_is_imm16(imm_data))
        host_arm64_MOVZ_IMM(block, reg, imm_data);
    else if (imm_is_imm16(inv))
        host_arm64_MOVN_IMM(block, reg, inv);     // NEW
    else {
        host_arm64_MOVZ_IMM(block, reg, imm_data & 0xffff);
        host_arm64_MOVK_IMM(block, reg, imm_data & 0xffff0000);
    }
}
```

This requires adding a `host_arm64_MOVN_IMM` emitter (encoding:
`OPCODE_MOVN_W = 0x12800000`).

**Impact**: Saves 1 instruction for mask-like values. Relevant for AND/OR
mask operations in the JIT, less so for LOAD_FUNC_ARG_IMM.

**Verdict**: Low priority but clean optimization. Worth adding if
`host_arm64_mov_imm` is being modified anyway.

### 4.6 Apple Silicon-Specific Considerations

The Apple Silicon CPU Optimization Guide (Version 4) provides several
recommendations relevant to JIT code generation:

1. **Do NOT align branch targets**: Apple Silicon handles alignment
   internally. NOP padding for alignment wastes I-cache space.

2. **NOP is free**: The NOP instruction is removed in the decoder and
   consumes no execution unit. However, it still occupies I-cache space.

3. **Favor smaller code size**: Apple's 192KB L1 I-cache is large, but
   smaller code still benefits from better cache utilization. This
   reinforces the value of Phase 2 (BL optimization) and Phase 3
   (mov_imm optimization).

4. **Pair-of-register instructions**: LDP/STP should be used where possible
   for load/store pairs. The existing prologue/epilogue already uses STP/LDP
   for callee-saved register save/restore.

Source: [Apple Silicon CPU Optimization Guide](https://developer.apple.com/documentation/apple-silicon/cpu-optimization-guide)

### 4.7 Software Prefetch Considerations

The plan includes `__builtin_prefetch` for block dispatch hash table
lookup (Phase 5, Section 5.2). Additional research confirms:

- **Cortex-A53**: Weak hardware prefetcher. Explicit prefetch provides
  significant benefit for pointer-chasing patterns like hash table lookup.
- **Apple Silicon**: Strong hardware prefetcher. Explicit prefetch provides
  marginal or no benefit and may interfere with the hardware prefetcher's
  stride detection.

The prefetch target is speculative (virtual address hash, not physical):

```c
__builtin_prefetch(&codeblock_hash[(cs + cpu_state.pc) & HASH_MASK], 0, 3);
```

The third argument (`3` = high temporal locality) tells the hardware to keep
the prefetched data in all cache levels. This is correct for the hash table
entry which will be accessed shortly after.

**Risk**: The worst case for a wrong prefetch is one wasted cache line fetch.
The best case is eliminating a cache miss on the critical path.

**Verdict**: Worth adding for Cortex-A53/A55 benefit. Neutral-to-slightly-
positive on Apple Silicon.

Source: [PRACE Best Practice Guide for ARM64](https://prace-ri.eu/wp-content/uploads/Best-Practice-Guide_ARM64.pdf)

---

<a id="recommendations"></a>
## 5. Final Recommendations (Prioritized by Impact)

### Priority 1: Proceed Immediately (Low Risk, Clear Benefit)

| Phase | Item | Impact | Risk | Effort |
|-------|------|--------|------|--------|
| 3 | Replace MOVX_IMM with mov_imm in LOAD_FUNC_ARG*_IMM | 1-2 insns saved per site (4 sites) | VERY LOW | 15 minutes |
| 5 | Add LIKELY/UNLIKELY to 386_dynarec.c hot paths | 1-10% on in-order cores | VERY LOW | 30 minutes |

Phase 3 is a trivial, safe change. Phase 5 annotations are advisory-only with
zero behavioral risk.

### Priority 2: Proceed with Validation (Low-Medium Risk, Moderate Benefit)

| Phase | Item | Impact | Risk | Effort |
|-------|------|--------|------|--------|
| 4 | Add CSEL_NE/GE/GT/LT/LE emitters | Enables branchless patterns | VERY LOW | 30 minutes |
| 4 | Add ADDS_IMM/SUBS_IMM emitters | Fuses ADD+CMP sequences | LOW | 45 minutes |
| 4 | Add CLZ/RBIT emitters | Inlines BSR/BSF emulation | LOW | 30 minutes |
| 5 | Branchless block validation | Eliminates 4-5 branches | LOW | 30 minutes |
| 5 | Add __builtin_prefetch for dispatch | Cache miss reduction | VERY LOW | 10 minutes |

### Priority 3: Consider Later (Lower Impact or Higher Effort)

| Item | Impact | Risk | Effort | Notes |
|------|--------|------|--------|-------|
| MOVN optimization in mov_imm | 1 insn for mask values | LOW | 30 minutes | Low frequency in practice |
| CSINC/CSINV/CSNEG emitters | Niche conditional ops | LOW | 20 minutes | Limited use in current IR |
| TBZ/TBNZ in JIT code | 1 insn per single-bit test | MEDIUM | 2-4 hours | Requires IR changes |
| Exception dispatch noinline (R6) | I-cache locality | MEDIUM | 1 hour | Needs inlining analysis |

### Priority 4: Confirmed Rejected (Do Not Implement)

| Item | Reason |
|------|--------|
| CCMP chaining in JIT | IR does not expose multi-condition patterns |
| ADRP+ADD for globals in JIT | ASLR makes distance unpredictable |
| Pinned readlookup2 register | Register pressure trade-off not worth it |
| MADD/MSUB fusion | IR does not expose MUL+ADD patterns |
| UDIV/SDIV for x86 DIV | Complex x86 semantics negate savings |

---

## Source Index

### ARM Architecture References

- [A64 Base Instructions (Stanford Mirror)](https://www.scs.stanford.edu/~zyedidia/arm64/)
- [A64 Base Instructions (ARM Developer)](https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions)
- [ARMv8 Instruction Set Overview](https://www.cs.princeton.edu/courses/archive/spr21/cos217/reading/ArmInstructionSetOverview.pdf)
- [ARMv8-A ISA Non-Confidential](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Armv8-A%20Instruction%20Set%20Architecture.pdf)
- [ARM64 Quick Reference (UW)](https://courses.cs.washington.edu/courses/cse469/19wi/arm64.pdf)
- [Encoding of Immediate Values on AArch64](http://dinfuehr.com/blog/encoding-of-immediate-values-on-aarch64/)

### Microarchitecture Details

- [ARM Cortex-A53 (7-cpu.com)](https://www.7-cpu.com/cpu/Cortex-A53.html)
- [ARM Cortex-A53: Tiny But Important (Chips and Cheese)](https://chipsandcheese.com/2023/05/28/arms-cortex-a53-tiny-but-important/)
- [Cortex-A53 Pipeline Stages (ARM Community)](https://community.arm.com/support-forums/f/architectures-and-processors-forum/12755/pipeline-stages-in-the-cortex-a53)
- [ARM Cortex-A72 Fetch and Branch](http://sandsoftwaresound.net/arm-cortex-a72-fetch-and-branch-processing/)
- [ARM Cortex-A72 Branch Mispredictions](http://sandsoftwaresound.net/arm-cortex-a72-tuning-branch-mispredictions/)
- [Cortex-A76 (WikiChip)](https://en.wikichip.org/wiki/arm_holdings/microarchitectures/cortex-a76)
- [AnandTech Cortex-A76](https://www.anandtech.com/show/12785/arm-cortex-a76-cpu-unveiled-7nm-powerhouse/2)
- [Apple M1 (7-cpu.com)](https://www.7-cpu.com/cpu/Apple_M1.html)
- [Apple M1 Firestorm Overview](https://dougallj.github.io/applecpu/firestorm.html)
- [Apple Silicon CPU Optimization Guide](https://developer.apple.com/documentation/apple-silicon/cpu-optimization-guide)

### Branch Prediction Research

- [Dissecting Branch Predictors of Apple Firestorm and Qualcomm Oryon](https://arxiv.org/html/2411.13900v1)
- [Branch Predictor: How Many IFs are Too Many? (Cloudflare)](https://blog.cloudflare.com/branch-predictor/)
- [LWN: Likely unlikely()s](https://lwn.net/Articles/420019/)
- [LWN: Profile likely and unlikely annotations](https://lwn.net/Articles/305323/)

### Compiler Optimization

- [Peeking Under the Hood of GCC's __builtin_expect](https://tbrindus.ca/how-builtin-expect-works/)
- [How Much Do __builtin_expect() Improve Performance?](http://blog.man7.org/2012/10/how-much-do-builtinexpect-likely-and.html)
- [Linux Kernel Newbies: LIKELY/UNLIKELY FAQ](https://kernelnewbies.org/FAQ/LikelyUnlikely)
- [The AArch64 processor, part 10: Loading Constants](https://devblogs.microsoft.com/oldnewthing/20220808-00/?p=106953)
- [The AArch64 processor, part 16: Conditional Execution](https://devblogs.microsoft.com/oldnewthing/20220817-00/?p=106998)

### JIT Compilation

- [JIT Compilation on ARM: Call-Site Code Consistency (ACM)](https://dl.acm.org/doi/10.1145/3546568)
- [.NET ARM64 JIT Work](https://github.com/dotnet/runtime/issues/43629)
- [V8 TurboFan CBZ/CBNZ/TBZ/TBNZ](https://groups.google.com/g/v8-dev/c/OCy_MZtLchQ)
- [Dolphin Emulator ARM64 JIT](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/PowerPC/JitArm64/JitArm64_SystemRegisters.cpp)

### FRECPE/FRSQRTE Precision

- [QEMU FEAT_RPRES Implementation](https://www.mail-archive.com/qemu-devel@nongnu.org/msg1092428.html)
- [FRSQRTE Reference](https://www.scs.stanford.edu/~zyedidia/arm64/frsqrte_advsimd.html)
