# Refactoring R1-R7: ARM64 JIT Backend Code Consolidation

**Branch**: `86box-arm64-cpu`
**Date**: 2026-02-18
**Files modified**: `codegen_backend_arm64_uops.c`, `386_dynarec.c`

## Overview

The ARM64 CPU JIT backend (`codegen_backend_arm64_uops.c`) contained ~900
lines of near-identical boilerplate across ~50 MMX/3DNow!/NEON UOP handler
functions. Each handler unpacked registers from a `uop_t`, asserted they
were Q-sized (64-bit NEON), and emitted a single ARM64 NEON instruction.
The only differences between handlers were the function name and the NEON
instruction emitted.

This refactoring introduces 5 parametric macros that capture the 3 handler
patterns (binary, unary, shift-by-immediate), reducing the file by ~710
lines while producing **identical object code** — the C preprocessor inlines
the macros at compile time with zero runtime overhead.

A separate change in `386_dynarec.c` moves cold exception-handling code
out of the hot interpreter loop for better I-cache behavior on ARM64.

---

## R1+R2: MMX/NEON Binary and Unary Handler Consolidation

### What changed

46 hand-written handler functions were replaced by single-line macro
invocations. Two macros were introduced:

**`DEFINE_MMX_BINARY_OP(OP_NAME, ARM64_INSN, FATAL_NAME)`**

Generates a handler for binary (3-register) NEON operations. The handler:
1. Unpacks `dest_reg`, `src_reg_a`, `src_reg_b` and their sizes from the uop
2. Asserts all three are `IREG_SIZE_Q` (64-bit MMX register width)
3. Emits one NEON instruction: `ARM64_INSN(block, dest, src_a, src_b)`
4. Calls `fatal()` if size assertion fails (should never happen in practice)

Used by 35 handlers: all PADD\*, PSUB\*, PCMPEQ\*, PCMPGT\*, PFADD, PFCMP\*,
PFMAX, PFMIN, PFMUL, PFSUB, PMULLW, and all PUNPCK\* operations.

**`DEFINE_MMX_UNARY_OP(OP_NAME, ARM64_INSN, FATAL_NAME)`**

Same pattern but for 2-register operations (one source, one destination).
Used by PF2ID and PI2FD (3DNow! float↔int conversions).

### What was NOT consolidated

Handlers with multi-instruction bodies were intentionally left as
hand-written functions because their logic cannot be captured by a simple
macro:

- **PFRCP** / **PFRSQRT**: 4-5 instruction Newton-Raphson sequences with
  register aliasing safety (REG_V_TEMP usage)
- **PMADDWD**: multiply-accumulate with widening and horizontal add
- **PMULHW**: multiply-high with narrowing (SMULL + SSHR + SQXTN)
- **PANDN**: bitwise AND-NOT (BIC) — operand order reversal

### Example: before vs after

Before (PADDB handler, 17 lines):
```c
static int
codegen_PADDB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) {
        host_arm64_ADD_V8B(block, dest_reg, src_reg_a, src_reg_b);
    } else
        fatal("PADDB %02x %02x %02x\n", uop->dest_reg_a_real,
              uop->src_reg_a_real, uop->src_reg_b_real);

    return 0;
}
```

After (1 line):
```c
DEFINE_MMX_BINARY_OP(PADDB, host_arm64_ADD_V8B, "PADDB")  /* x86 PADDB -> NEON ADD 8B */
```

The macro expands to exactly the same code. Every handler follows this
identical pattern — the only variable parts are the function name, the NEON
emitter, and the diagnostic string.

---

## R3: Shift-Immediate Handler Factory

### What changed

9 shift-by-immediate handlers (3 shift types × 3 element widths) were
consolidated into 3 shift-specific macros:

**`DEFINE_MMX_SHIFT_LEFT_IMM(OP_NAME, ARM64_SHL, MAX_SHIFT, FATAL_NAME)`**

Handles PSLLW/PSLLD/PSLLQ (left shift). Three cases:
- **shift == 0**: `FMOV_D_D` (copy, no-op shift)
- **shift >= element width**: `EOR` with self (zero the register — x86 behavior)
- **otherwise**: `SHL` by the immediate amount

**`DEFINE_MMX_SHIFT_RIGHT_ARITH_IMM(OP_NAME, ARM64_SSHR, MAX_SHIFT, FATAL_NAME)`**

Handles PSRAW/PSRAD/PSRAQ (arithmetic right shift). Three cases:
- **shift == 0**: `FMOV_D_D` (copy)
- **shift >= element width**: `SSHR` by (width-1), filling with sign bit
  (x86 PSRA\* behavior — excessive shifts replicate the sign bit)
- **otherwise**: `SSHR` by the immediate amount

**`DEFINE_MMX_SHIFT_RIGHT_LOGIC_IMM(OP_NAME, ARM64_USHR, MAX_SHIFT, FATAL_NAME)`**

Handles PSRLW/PSRLD/PSRLQ (logical right shift). Three cases:
- **shift == 0**: `FMOV_D_D` (copy)
- **shift >= element width**: `EOR` with self (zero — same as left-shift overflow)
- **otherwise**: `USHR` by the immediate amount

### Consolidated handlers

| Macro invocation | x86 op | NEON op | Element width |
|---|---|---|---|
| `DEFINE_MMX_SHIFT_LEFT_IMM(PSLLW_IMM, ..._SHL_V4H, 16, ...)` | PSLLW | SHL 4H | 16-bit |
| `DEFINE_MMX_SHIFT_LEFT_IMM(PSLLD_IMM, ..._SHL_V2S, 32, ...)` | PSLLD | SHL 2S | 32-bit |
| `DEFINE_MMX_SHIFT_LEFT_IMM(PSLLQ_IMM, ..._SHL_V2D, 64, ...)` | PSLLQ | SHL 2D | 64-bit |
| `DEFINE_MMX_SHIFT_RIGHT_ARITH_IMM(PSRAW_IMM, ..._SSHR_V4H, 16, ...)` | PSRAW | SSHR 4H | 16-bit |
| `DEFINE_MMX_SHIFT_RIGHT_ARITH_IMM(PSRAD_IMM, ..._SSHR_V2S, 32, ...)` | PSRAD | SSHR 2S | 32-bit |
| `DEFINE_MMX_SHIFT_RIGHT_ARITH_IMM(PSRAQ_IMM, ..._SSHR_V2D, 64, ...)` | PSRAQ | SSHR 2D | 64-bit |
| `DEFINE_MMX_SHIFT_RIGHT_LOGIC_IMM(PSRLW_IMM, ..._USHR_V4H, 16, ...)` | PSRLW | USHR 4H | 16-bit |
| `DEFINE_MMX_SHIFT_RIGHT_LOGIC_IMM(PSRLD_IMM, ..._USHR_V2S, 32, ...)` | PSRLD | USHR 2S | 32-bit |
| `DEFINE_MMX_SHIFT_RIGHT_LOGIC_IMM(PSRLQ_IMM, ..._USHR_V2D, 64, ...)` | PSRLQ | USHR 2D | 64-bit |

---

## R4: HOST_REG_GET Boilerplate Macro

**Status: Addressed by R1-R3.**

The 46 handlers consolidated by R1-R3 accounted for the majority of the
repeated register-unpacking boilerplate. The remaining ~50 handlers (ADD,
SUB, AND, OR, MOV, CMP, etc.) use the unpacked register and size values in
complex, size-dependent dispatch logic (e.g., different code paths for
8-bit, 16-bit, 32-bit, 64-bit operands). A simple `UNPACK_UOP_BINARY` macro
would save only the 6 declaration lines but not the dispatch logic, making
the net savings too small to justify the added indirection.

---

## R5: Load/Store Stub Generalization

**Status: Deferred.**

`build_load_routine()` and `build_store_routine()` in
`codegen_backend_arm64.c` are ~165 lines each. They differ in:

1. **Register allocation**: loads use X1/X2, stores use X2/X3
2. **Float conversion ordering**: loads convert after the slow-path call,
   stores convert before it
3. **Memory access direction**: loads read from emulated memory, stores
   write to it

Merging them into a parametric function would save ~80 LOC but require
careful handling of these differences. Since these stubs execute on every
JIT memory access (thousands of times per second), any subtle bug would
cause hard-to-diagnose memory corruption. The risk/reward ratio is
unfavorable.

---

## R6: Exception Dispatch Tail Call

### What changed

In `386_dynarec.c`, the SMI/NMI/IRQ exception handling code (~30
instructions spanning 2-3 I-cache lines) was extracted from the hot
`exec386_dynarec()` loop into a separate function:

```c
static __attribute__((noinline)) void
handle_pending_exceptions(void)
{
    /* SMI, NMI, IRQ dispatch — ~30 instructions of cold code */
}
```

The hot loop now contains only:
```c
#if defined(__aarch64__) || defined(_M_ARM64)
if (UNLIKELY(smi_line || (nmi && nmi_enable && nmi_mask)
             || ((cpu_state.flags & I_FLAG) && pic.int_pending)))
    handle_pending_exceptions();
#else
    /* Original inline code preserved for other architectures */
#endif
```

### Why `__attribute__((noinline))`

Without `noinline`, the compiler may inline the function back into the
caller, defeating the purpose. The `noinline` attribute guarantees the
exception code lives in a separate code region. The `UNLIKELY` macro tells
the compiler to place the call on the cold path (typically after the
function epilogue), so the hot loop body stays compact.

### Why ARM64 only

On x86-64, the compiler and hardware branch predictor are generally good
enough that this optimization provides negligible benefit. ARM64 in-order
cores (Cortex-A53/A55) benefit more from reduced I-cache pressure because
they have smaller L1 I-caches (32-48KB) and no micro-op cache.

### Performance impact

Saves 2-3 I-cache lines in the hot path. The actual cycle savings are
small (exceptions fire ~100-1000x/sec vs. millions of block dispatches/sec)
but the I-cache improvement benefits all code sharing those cache lines.

---

## R7: PUNPCKLDQ / ZIP1 Endianness Verification

### Finding: VERIFIED CORRECT

ARM64 NEON `ZIP1`/`ZIP2` instructions are semantically equivalent to x86
`PUNPCKL*`/`PUNPCKH*` instructions on little-endian systems.

### Reasoning

Both instruction families interleave elements from two source vectors:

| Operation | x86 | ARM64 NEON |
|---|---|---|
| Interleave low elements | PUNPCKL{BW,WD,DQ} | ZIP1.{8B,4H,2S} |
| Interleave high elements | PUNPCKH{BW,WD,DQ} | ZIP2.{8B,4H,2S} |

On little-endian ARM64 (which is the only byte order supported in user
mode), NEON element numbering matches x86 — element 0 is at the lowest
memory address / least significant bits. Therefore:

```
ZIP1 Vd.2S, Vn.2S, Vm.2S  =>  Vd = [Vn[0], Vm[0]]
x86  PUNPCKLDQ mm1, mm2    =>  mm1 = [mm1[0], mm2[0]]
```

These are identical. No code change was needed. The verification is
documented as a comment block in `codegen_backend_arm64_uops.c` above the
PUNPCK handler group (line ~1869).

---

## Summary

| Item | Status | LOC saved | Risk | Object code change |
|---|---|---|---|---|
| R1 | Done | ~500 | LOW | None (macro expansion) |
| R2 | Done (folded into R1) | ~150 | LOW | None |
| R3 | Done | ~160 | LOW | None |
| R4 | Addressed by R1-R3 | — | — | None |
| R5 | Deferred | — | MEDIUM | N/A |
| R6 | Done | ~20 (net) | MEDIUM | ARM64 only: cold code moved out-of-line |
| R7 | Verified correct | 0 | — | None |

**Total LOC reduction**: ~710 lines from `codegen_backend_arm64_uops.c`
**Behavioral changes**: None. All macros expand to identical code. R6 is the
only item that changes code layout (ARM64 only), and it is functionally
equivalent — the same exception handlers run in the same order.

## Commits

1. `d56d965cd` — R1-R3: Consolidate 46 MMX/3DNow!/NEON handlers with parametric macros
2. `79bbfb7ea` — R6: Move exception dispatch to noinline function on ARM64
3. `bf7cf87ac` — Update checklist/changelog for R1-R7, apply clang-format
