# ARM64 CPU JIT Backend Optimization Plan

## Executive Summary

This plan targets the NEW_DYNAREC ARM64 JIT backend in 86Box, covering both
JIT code generation quality improvements and C-level interpreter/dispatch
optimizations. The plan is organized into six phases, ordered by
risk-vs-reward ratio (safest and highest-impact first).

**Target**: ARMv8.0 baseline (no Apple Silicon-specific features). All
optimizations must work correctly on Cortex-A53/A55 (in-order), Cortex-A72/A76
(OOO), Apple M-series, Graviton, Ampere Altra, and any conformant ARMv8.0+
implementation.

**Total estimated instruction savings in generated JIT code**: 3-5 instructions
per function call site (23 call sites in uops + 9 in stubs + 13 CBNZ exit
checks), plus 3-4 instructions per PFRCP/PFRSQRT (with huge latency reduction),
plus better branch prediction in the interpreter hot loop.

## Generic ARM64 Considerations

Several optimizations in this plan have **different impact levels** depending on
the ARM64 microarchitecture. Key differences:

| Feature | Apple M-series | Cortex-A53/A55 | Cortex-A72/A76+ |
|---------|---------------|----------------|-----------------|
| Branch predictor | Deep OOO, TAGE | In-order, simple | OOO, decent |
| L1 I-cache | ~192KB | 32KB | 48-64KB |
| HW prefetcher | Strong | Weak | Moderate |
| FRECPE/FRSQRTE precision | ~12 bits | ~8 bits (ARM min) | ~8-12 bits |
| FDIV_S latency | 10-13 cycles | 20-30 cycles | 12-18 cycles |
| FSQRT_S latency | 11-14 cycles | 25-35 cycles | 14-20 cycles |

**Critical implication**: Newton-Raphson refinement for FRECPE/FRSQRTE is
**mandatory** (not optional) because Cortex-A53 may produce only 8-bit
precision, which is below AMD 3DNow!'s 14-15 bit guarantee.

**Performance implications**: Branch hints (Phase 5) and code size reductions
(Phase 2) have **significantly higher impact** on in-order cores and cores with
smaller I-caches than on Apple Silicon.

---

## Phase 1: PFRSQRT Bug Fix + 3DNow! FRECPE/FRSQRTE (Bug Fix + Quick Win)

### 1.1 PFRSQRT Register Clobber Bug (Critical)

**File**: `src/codegen_new/codegen_backend_arm64_uops.c` lines 1867-1885

The current `codegen_PFRSQRT` has a register clobber bug:

```c
host_arm64_FSQRT_S(block, REG_V_TEMP, src_reg_a);   // V_TEMP = sqrt(src)
host_arm64_FMOV_S_ONE(block, REG_V_TEMP);            // V_TEMP = 1.0 -- CLOBBERS sqrt!
host_arm64_FDIV_S(block, dest_reg, dest_reg, REG_V_TEMP); // dest = dest / 1.0 (wrong!)
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

The `FMOV_S_ONE` at line 1879 overwrites the sqrt result in `REG_V_TEMP` that
was just computed at line 1878. The third line then divides `dest_reg` by 1.0
(no-op) instead of dividing 1.0 by sqrt(src).

**Fix**: Change line 1879 to write to `dest_reg` instead of `REG_V_TEMP`, and
line 1880 to `FDIV_S(dest_reg, dest_reg, REG_V_TEMP)`:

```c
host_arm64_FSQRT_S(block, REG_V_TEMP, src_reg_a);   // V_TEMP = sqrt(src)
host_arm64_FMOV_S_ONE(block, dest_reg);              // dest = 1.0
host_arm64_FDIV_S(block, dest_reg, dest_reg, REG_V_TEMP); // dest = 1.0 / sqrt(src)
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);
```

**Risk**: LOW (pure bug fix, no behavioral change for correct code)
**Impact**: Correctness fix for all 3DNow! software using PFRSQRT

### 1.2 FRECPE/FRSQRTE for PFRCP/PFRSQRT

**File**: `src/codegen_new/codegen_backend_arm64_ops.c` (add new opcodes)
**File**: `src/codegen_new/codegen_backend_arm64_ops.h` (add new emitters)
**File**: `src/codegen_new/codegen_backend_arm64_uops.c` (replace handlers)

The 3DNow! `PFRCP` (reciprocal) and `PFRSQRT` (reciprocal square root) are
approximate instructions -- they produce 14-bit precision results on real
hardware. The current implementation uses exact FDIV and FSQRT, which are
dramatically slower than necessary.

ARM64 provides perfect semantic matches:
- **FRECPE** (opcode `0x0ea1d800` for V2S): Floating-point reciprocal estimate,
  ~12-bit accuracy. Latency: 2-4 cycles on Apple Silicon.
- **FRSQRTE** (opcode `0x2ea1d800` for V2S): Floating-point reciprocal sqrt
  estimate, ~12-bit accuracy. Latency: 2-4 cycles on Apple Silicon.

Compare to current:
- `PFRCP`: FMOV_S_ONE + FDIV_S + DUP_V2S = 3 insns, **FDIV_S latency: 10-13 cycles**
- `PFRSQRT`: FSQRT_S + FMOV_S_ONE + FDIV_S + DUP_V2S = 4 insns, **FSQRT_S latency: 11-14 cycles + FDIV_S: 10-13 cycles**

With FRECPE/FRSQRTE + mandatory Newton-Raphson refinement:
- `PFRCP`: FRECPE + FRECPS + FMUL + DUP_V2S = 4 insns, **latency: ~6 cycles**
- `PFRSQRT`: FRSQRTE + FRSQRTS + FMUL + DUP_V2S = 4 insns, **latency: ~6 cycles**

**Newton-Raphson refinement is MANDATORY.** The ARM spec only guarantees 8-bit
minimum precision for FRECPE/FRSQRTE. On Cortex-A53 cores, the raw estimate may
produce only ~8 bits, which is below AMD 3DNow!'s 14-15 bit guarantee. One
Newton-Raphson step doubles precision to ~16 bits, safely exceeding the AMD spec
on ALL conformant ARMv8.0 implementations.

PFRCP with refinement (aliasing-safe: estimate goes to REG_V_TEMP):
```c
host_arm64_FRECPE_V2S(block, REG_V_TEMP, src_reg_a);           // temp = x0 ≈ 1/src (src preserved)
host_arm64_FRECPS_V2S(block, dest_reg, REG_V_TEMP, src_reg_a); // dest = 2 - x0*src (~16 bit)
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, dest_reg);    // dest = x0 * (2 - x0*src)
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);               // broadcast
```

PFRSQRT with refinement (aliasing-safe: compute x0\*a, not x0², per aliasing-audit Option B):
```c
host_arm64_FRSQRTE_V2S(block, REG_V_TEMP, src_reg_a);          // temp = x0 ≈ 1/sqrt(src) (src preserved)
host_arm64_FMUL_V2S(block, dest_reg, REG_V_TEMP, src_reg_a);   // dest = x0*src (last read of src)
host_arm64_FRSQRTS_V2S(block, dest_reg, dest_reg, REG_V_TEMP); // dest = (3 - x0*src*x0) / 2
host_arm64_FMUL_V2S(block, dest_reg, dest_reg, REG_V_TEMP);    // dest = step * x0 (~16 bit)
host_arm64_DUP_V2S(block, dest_reg, dest_reg, 0);               // broadcast
```

**New opcodes needed**:
```c
#define OPCODE_FRECPE_V2S  (0x0ea1d800)  // FRECPE Vd.2S, Vn.2S
#define OPCODE_FRSQRTE_V2S (0x2ea1d800)  // FRSQRTE Vd.2S, Vn.2S
#define OPCODE_FRECPS_V2S  (0x0e20fc00)  // FRECPS Vd.2S, Vn.2S, Vm.2S (Newton-Raphson)
#define OPCODE_FRSQRTS_V2S (0x0ea0fc00)  // FRSQRTS Vd.2S, Vn.2S, Vm.2S (Newton-Raphson)
#define OPCODE_FMUL_V2S    (0x2e20dc00)  // FMUL Vd.2S, Vn.2S, Vm.2S
```

**Instruction count**: PFRCP goes from 3 → 4 insns, PFRSQRT from 4 → 5 insns
(+1 each for refinement). But latency drops dramatically:

| Operation | Current latency | Optimized latency | Speedup |
|-----------|----------------|-------------------|---------|
| PFRCP | 10-13 cyc (Apple) / 20-30 cyc (A53) | ~6 cyc (all cores) | 2-5x |
| PFRSQRT | 21-27 cyc (Apple) / 45-65 cyc (A53) | ~6 cyc (all cores) | 4-10x |

**Risk**: LOW (Newton-Raphson exceeds AMD precision spec on all ARMv8.0 cores)

---

## Phase 2: PC-Relative BL for Intra-Pool Stub Calls (High Impact)

### 2.1 Problem

Every call to a load/store stub or external C function goes through:
```
host_arm64_MOVX_IMM(block, REG_X16, addr)  // 1-4 instructions
host_arm64_BLR(block, REG_X16)             // 1 instruction
```

For a typical 48-bit macOS pointer like `0x0001_ABCD_EF00`, MOVX_IMM emits:
```asm
MOVZ X16, #0xEF00             // 1 insn
MOVK X16, #0xABCD, LSL #16   // 1 insn
MOVK X16, #0x0001, LSL #32   // 1 insn
BLR X16                       // 1 insn
```
= **4 instructions per call** (sometimes 3 if the upper half-word is zero,
sometimes 5 if all 4 half-words are non-zero).

### 2.2 Intra-Pool BL Optimization

The JIT memory pool is a single contiguous 120MB mmap
(`MEM_BLOCK_NR=131072 * MEM_BLOCK_SIZE=0x3c0`). The ARM64 BL instruction has
+/-128MB range, so any intra-pool call is guaranteed to be in range.

**Targets for BL optimization** (call targets that are within the pool):
- `codegen_mem_load_byte/word/long/quad/single/double` (6 stubs)
- `codegen_mem_store_byte/word/long/quad/single/double` (6 stubs)
- `codegen_fp_round/codegen_fp_round_quad` (2 stubs)

Note: `codegen_gpf_rout` is intra-pool but is a **branch target** (reached via
CBNZ), not a BL call target. It is NOT converted to `host_arm64_call_intrapool`.

> **Correction (validated 2026-02-18):** Validation confirmed `codegen_gpf_rout` is reached exclusively via CBNZ branches from UOP handlers (e.g., `host_arm64_CBNZ(block, REG_X1, ...)`), not via direct `host_arm64_call` invocations. It is a branch target, not a BL call target. The note above is correct; the earlier mention in the executive summary's "call sites" context could be misleading. See `codegen_backend_arm64.c` lines 312-315.

These are called from 26 sites in `codegen_backend_arm64_uops.c`.

**New function**: `host_arm64_call_intrapool`
```c
void host_arm64_call_intrapool(codeblock_t *block, void *dest)
{
    int offset;
    codegen_alloc(block, 4);  // MUST pre-reserve BEFORE capturing PC
    offset = (uintptr_t) dest - (uintptr_t) &block_write_data[block_pos];
    if (!offset_is_26bit(offset))
        fatal("host_arm64_call_intrapool - offset out of range %x\n", offset);
    codegen_addlong(block, OPCODE_BL | OFFSET26(offset));
}
```

**CRITICAL safety note**: The `codegen_alloc(block, 4)` call MUST happen before
computing the offset. If `codegen_alloc` triggers `codegen_allocate_new_block`,
then `block_write_data` and `block_pos` change, and any previously computed
offset would be wrong. The implementation above calls `codegen_alloc` FIRST,
then computes the offset from the updated `block_write_data[block_pos]`.

Then modify all 23 call sites in `codegen_backend_arm64_uops.c` that call
`host_arm64_call(block, codegen_mem_load_*)` / `codegen_mem_store_*` /
`codegen_fp_round*` to use `host_arm64_call_intrapool` instead.

> **Correction (validated 2026-02-18):** The actual verified count is 26 intra-pool call sites (not 23), plus 6 external C function calls left unchanged. Independently confirmed via grep: 26 `host_arm64_call_intrapool` + 6 `host_arm64_call`. (validation.md §4.6, §7.1.4)

The stubs' internal calls to external C functions (readmembl, writemembl, etc.)
remain MOVX_IMM+BLR since those are NOT in the JIT pool.

**Instruction savings per stub call**: 2-4 instructions (from 3-5 down to 1)
**Total savings**: With 23 stub call sites, each emitted per recompiled block,
this saves **46-92 instructions per block** in generated code size.

> **Correction (validated 2026-02-18):** Per the corrected count above, the actual total savings are based on 26 stub call sites, not 23.

**Risk**: MEDIUM (offset computation must be correct; see safety note above)

### 2.3 host_arm64_jump Optimization

`host_arm64_jump` (used by `codegen_JMP`) currently uses MOVX_IMM+BR (up to 5
insns). Since jump targets are always within the pool, this can be replaced with
a single B instruction:

```c
void host_arm64_jump(codeblock_t *block, uintptr_t dst_addr)
{
    host_arm64_B(block, (void *) dst_addr);
}
```

**Instruction savings**: 3-4 instructions per JMP (from 4-5 down to 1)
**Risk**: LOW (host_arm64_B already validates the 26-bit offset)

---

## Phase 3: LOAD_FUNC_ARG*_IMM Width Optimization (Quick Win)

### Problem

The `codegen_LOAD_FUNC_ARG0_IMM` through `codegen_LOAD_FUNC_ARG3_IMM` handlers
use `host_arm64_MOVX_IMM` (64-bit immediate load, up to 4 instructions) for
values that are typically small 32-bit immediates (opcode values, flag constants,
etc.):

```c
host_arm64_MOVX_IMM(block, REG_ARG0, uop->imm_data);  // Up to 4 insns for small values!
```

Note: `uop->imm_data` is `uintptr_t` (64-bit) on ARM64, but all callers pass
values that fit in 32 bits. The optimization is safe in practice.
Replace with `host_arm64_mov_imm`:

```c
host_arm64_mov_imm(block, REG_ARG0, uop->imm_data);  // 1-2 insns for 32-bit values
```

**Affected sites**: Lines 853, 860, 867, 874 in `codegen_backend_arm64_uops.c`
**Instruction savings**: 1-2 instructions per LOAD_FUNC_ARG*_IMM
**Risk**: VERY LOW (imm_data is uint32_t, cannot exceed 32 bits)

> **Correction (validated 2026-02-18):** `uop->imm_data` is NOT `uint32_t` on ARM64. It is `uintptr_t` (64-bit), as defined in `codegen_ir_defs.h` lines 339-343 under the guard `#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM || defined __aarch64__ || defined _M_ARM64`. On x86-64 it is `uint32_t`. The optimization is still safe because all callers pass values that fit in 32 bits, and `host_arm64_mov_imm` takes `uint32_t` — the truncation is harmless. The x86-64 backend confirms this by using `host_x86_MOV32_REG_IMM` (32-bit MOV) for the same handlers.

---

## Phase 4: New ARM64 Emitters (Moderate Effort)

### 4.1 CSEL with More Conditions

Currently only CSEL_CC, CSEL_EQ, CSEL_VS are implemented. Missing:
- **CSEL_NE**: Would eliminate branches in some comparison sequences
- **CSEL_GE/GT/LT/LE**: For signed comparison results
- **CSINC/CSINV/CSNEG**: Conditional increment/invert/negate

### 4.2 ADDS/SUBS (Flag-Setting Add/Subtract)

Currently ADD and SUB never set flags. Adding ADDS_REG/SUBS_REG emitters would
allow fusing an ADD + CMP sequence into a single ADDS (when the comparison is
against 0 or against the result of the addition).

Note: CMP is actually SUBS with Rd=XZR, and CMN is ADDS with Rd=XZR. So the
infrastructure is partially there -- we just need wrappers that write to a real
destination register.

### 4.3 CLZ (Count Leading Zeros)

Would optimize `BSR`/`BSF` x86 instruction emulation. Currently these are
handled by calling out to C functions. CLZ can be done in a single instruction.

**Opcode**: `0x5ac01000`

### 4.4 MADD/MSUB (Multiply-Add/Subtract) — Lower Priority

**Opcode**: MADD = `0x1b000000`, MSUB = `0x1b008000`

Would fuse x86 `IMUL + ADD` sequences into a single instruction. The IR likely
does not expose this pattern often enough to justify the effort.

### Instruction savings: Variable (1-3 per pattern match)
### Risk: LOW-MEDIUM (new emitters, need encoding validation)

---

## Phase 5: C-Level Interpreter/Dispatch Optimizations

### 5.1 LIKELY/UNLIKELY Branch Hints

**File**: `src/cpu/386_dynarec.c`

```c
// Inner loop condition (LIKELY true)
while (LIKELY(cycles > 0)) {

// JIT path is the common case (LIKELY false for force_interpreter)
if (UNLIKELY(cpu_force_interpreter || cpu_override_dynarec || !CACHE_ON())) {

// Block execution is the common case (LIKELY true)
if (LIKELY(valid_block && (block->flags & CODEBLOCK_WAS_RECOMPILED))) {

// Abort is rare (UNLIKELY)
if (UNLIKELY(cpu_state.abrt)) {
```

**File**: `src/cpu/386_common.h`

```c
// Page boundary crossing is rare (LIKELY true)
if (LIKELY((a & 0xFFF) < 0xFFD)) {

// Page cache hit is common (UNLIKELY to miss)
if (UNLIKELY((a >> 12) != pccache)) {
```

Note: `LIKELY`/`UNLIKELY` macros already exist in `src/include/86box/86box.h`
(lines 94-95).

**Risk**: VERY LOW (purely advisory, no behavioral change)

**Generic ARM64 note**: Branch hints have significantly higher impact on
in-order cores (Cortex-A53/A55) where misprediction stalls the entire pipeline.
On Apple Silicon, the deep OOO pipeline and TAGE predictor handle most branches
well. On Cortex-A53, expect 5-10% improvement vs 1-3% on Apple Silicon.

### 5.2 Software Prefetch for Block Dispatch

**File**: `src/cpu/386_dynarec.c`

Prefetch the hash table entry based on the virtual address hash as a speculative
hint before the expensive `get_phys()` call:

```c
__builtin_prefetch(&codeblock_hash[(cs + cpu_state.pc) & HASH_MASK], 0, 3);
```

**Risk**: VERY LOW (prefetch is a hint; wrong prefetch wastes a cache line at worst)

**Generic ARM64 note**: Cortex-A53 has weak hardware prefetching — explicit
`__builtin_prefetch` provides significantly more benefit than on Apple Silicon
which has strong HW prefetchers.

### 5.3 Block Validation Branchless Pattern

Replace `&&` chain with bitwise OR of XOR results to eliminate 3-4 branches:

```c
uint32_t pc_match     = block->pc ^ (cs + cpu_state.pc);
uint32_t cs_match     = block->_cs ^ cs;
uint32_t phys_match   = block->phys ^ phys_addr;
uint32_t status_match = (block->status ^ cpu_cur_status) & CPU_STATUS_FLAGS;
valid_block = !(pc_match | cs_match | phys_match | status_match);
```

**Risk**: LOW (semantically equivalent)

**Generic ARM64 note**: On in-order Cortex-A53/A55, branchless is a clear win
since there is no speculative execution to hide branch misprediction cost.
On OOO cores, the branch predictor may handle the &&-chain just fine, but
branchless is never worse and helps worst-case scenarios.

---

## Audit Findings — Additional Items

The following items were discovered during a deep code audit and are tracked
separately from the main optimization phases.

### ADD_LSHIFT Missing Size Validation

**File**: `src/codegen_new/codegen_backend_arm64_uops.c` lines 94-98
**Severity**: MEDIUM

`codegen_ADD_LSHIFT` does not validate register sizes (W/B/BH) unlike
`codegen_ADD`. It always emits a full-width ADD with shift. If the IR generates
an `ADD_LSHIFT` uop for a sub-register operation, upper bits would be corrupted.

**Action**: Investigate whether the IR ever generates `ADD_LSHIFT` for byte/word
operands. If so, add size validation matching `codegen_ADD`. If not, add a
comment documenting the assumption.

### Source TODO Comments

Two TODO comments in the source code at lines 1859 and 1877 of
`codegen_backend_arm64_uops.c` note that PFRCP/PFRSQRT could use
VRECPE/VRSQRTE. These are addressed by Phase 1 of this plan.

### Non-Issues Confirmed

| Item | Assessment |
|------|-----------|
| X16/X17 register clobber | Non-issue — these are ARM64 IP0/IP1, reserved for scratch. Allocator never assigns guest regs to them. |
| REG_CPUSTATE = X29 | Non-issue — JIT doesn't use frame pointer-based stack frames. Safe repurposing. |
| Offset range macros unsigned-only | Correct — matches ARM64 `LDR/STR #uimm12` encoding. All cpu_state offsets are positive. |
| MOVK_IMM strict validation | Correct — caller splits immediates into half-words before passing. |
| in_range7_x naming | Correct — refers to `imm7` field in STP/LDP encoding, not a 7-bit range. |

---

## Phase 6: Investigated and Rejected

| Optimization | Reason for Rejection |
|---|---|
| **Pinned readlookup2 register** | Sacrifices one of 10 host registers; register pressure trade-off not worth it |
| **ADRP+ADD for globals** | macOS ASLR makes JIT-to-global distance unpredictable; cannot guarantee +/-4GB |
| **Constant pool with LDR literal** | Block allocator chaining makes placement difficult; minimal gain |
| **CCMP chaining in JIT code** | IR doesn't expose multi-condition patterns; would need IR-level fusion |
| **TBZ/TBNZ in JIT code** | Already used in stubs via TST_IMM+BNE; IR comparison uops don't distinguish single-bit tests |
| **LDP for readlookup2+writelookup2** | Arrays not adjacent in memory; would require struct reorganization |
| **UDIV/SDIV for x86 DIV** | x86 DIV has complex semantics (flags, exception on zero, 64:32 division); guard code negates savings |
| **MADD/MSUB fusion** | IR doesn't expose MUL+ADD patterns; would need peephole optimizer at IR level |

---

## Refactoring Opportunities

The following refactoring items are independent of the optimization phases above.
They improve code quality, maintainability, and in some cases I-cache behavior,
but do not change generated JIT code.

### R1: MMX/NEON Handler Template Consolidation

**File**: `src/codegen_new/codegen_backend_arm64_uops.c` lines 1494-2370
**Estimated savings**: ~400 lines

143 MMX/3DNow! handler functions follow identical patterns for different element
sizes (e.g., PADDB/PADDW/PADDD, PSUBB/PSUBW/PSUBD, PCMPEQB/W/D). These can
be consolidated using a parametric macro:

```c
#define CODEGEN_MMX_BINARY_OP(OP_NAME, ARM64_INSN) \
    static int codegen_##OP_NAME(codeblock_t *block, uop_t *uop) { \
        int dest_reg = HOST_REG_GET(uop->dest_reg_a_real); \
        int src_a = HOST_REG_GET(uop->src_reg_a_real); \
        int src_b = HOST_REG_GET(uop->src_reg_b_real); \
        ARM64_INSN(block, dest_reg, src_a, src_b); \
        return 0; \
    }
```

**Risk**: LOW — macros expand to identical object code

### R2: Comparison Operation Consolidation

**File**: `src/codegen_new/codegen_backend_arm64_uops.c` lines 1614-1714
**Estimated savings**: ~150 lines

PCMPEQB/W/D and PCMPGTB/W/D handlers share identical dispatch logic. Can be
folded into the same macro template as R1.

### R3: Shift-Immediate Handler Factory

**File**: `src/codegen_new/codegen_backend_arm64_uops.c` lines 1975-2152
**Estimated savings**: ~150-200 lines

9 shift types × 3 sizes = 27 handlers with identical structure. Could use a
dispatch table or macro factory.

### R4: HOST_REG_GET Boilerplate Macro

**File**: `src/codegen_new/codegen_backend_arm64_uops.c` (98 occurrences)
**Estimated savings**: ~200 lines

The same 6-line register unpacking block appears at the top of nearly every
handler. Could be replaced with:

```c
#define UNPACK_UOP_BINARY(uop, dest, src_a, src_b) \
    int dest = HOST_REG_GET((uop)->dest_reg_a_real); \
    int src_a = HOST_REG_GET((uop)->src_reg_a_real); \
    int src_b = HOST_REG_GET((uop)->src_reg_b_real); \
    int dest##_size = IREG_GET_SIZE((uop)->dest_reg_a_real); \
    int src_a##_size = IREG_GET_SIZE((uop)->src_reg_a_real); \
    int src_b##_size = IREG_GET_SIZE((uop)->src_reg_b_real)
```

**Risk**: VERY LOW

### R5: Load/Store Stub Generalization

**File**: `src/codegen_new/codegen_backend_arm64.c` lines 85-250
**Estimated savings**: ~80 lines

`build_load_routine()` and `build_store_routine()` are ~165 lines each with
only size/direction differences. Could be parametrized with a variant table.

**Risk**: MEDIUM — stubs are correctness-critical

### R6: Exception Dispatch Tail Call

**File**: `src/cpu/386_dynarec.c` lines 812-823

Move SMI/NMI/IRQ exception handling to a separate `__attribute__((noinline))`
function. This keeps the hot path (normal block execution) compact in L1
I-cache and pushes the cold exception path out of line.

```c
static __attribute__((noinline)) void
handle_pending_exceptions(void) { /* SMI, NMI, IRQ dispatch */ }

// In main loop:
if (UNLIKELY(smi_line || (nmi && nmi_enable && nmi_mask) ||
             ((cpu_state.flags & I_FLAG) && pic.int_pending)))
    handle_pending_exceptions();
```

**Risk**: MEDIUM — requires inlining analysis

### R7: PUNPCKLDQ/ZIP1 Endianness Verification

**File**: `src/codegen_new/codegen_backend_arm64_uops.c` line 2365

ARM64 NEON `ZIP1 Vd.2S, Vn.2S, Vm.2S` interleaves low 32-bit elements to
produce `[Vn[0], Vm[0]]`. x86 `PUNPCKLDQ` does the same. These should be
semantically equivalent on little-endian ARM64, but this needs explicit
verification against the NEON spec (section 6.11) or a hardware test.

**Risk**: MEDIUM — if wrong, affects all MMX interleave operations

---

## Key Reference Files

| File | What to look at |
|------|----------------|
| `src/include/86box/86box.h:92-97` | Existing LIKELY/UNLIKELY macros |
| `src/cpu/386_common.h` | Instruction fetch, memory macros, EA calculation |
| `src/cpu/386_dynarec.c` | Interpreter loop, JIT dispatch, block validation |
| `src/codegen_new/codegen_backend_arm64.c` | JIT prologue/epilogue, memory stubs, `build_load_routine`, `build_store_routine` |
| `src/codegen_new/codegen_backend_arm64_uops.c` | IR-to-native translation handlers — 23 stub call sites, PFRCP/PFRSQRT handlers |
| `src/codegen_new/codegen_backend_arm64_ops.c` | ARM64 instruction emission, `host_arm64_call`, `host_arm64_MOVX_IMM`, `host_arm64_ADRP_ADD` |
| `src/codegen_new/codegen_backend_arm64_ops.h` | Instruction emitter declarations |
| `src/codegen_new/codegen_backend_arm64_defs.h` | Register definitions |
| `src/codegen_new/codegen_allocator.c` | JIT pool allocation — single ~120MB mmap |
| `src/codegen_new/codegen_allocator.h` | `MEM_BLOCK_NR=131072`, `MEM_BLOCK_SIZE=0x3c0` |
