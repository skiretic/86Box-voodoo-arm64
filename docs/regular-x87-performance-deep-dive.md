# Regular x87 Performance Deep Dive

## Scope

This document is a static investigation of the regular x87/FPU emulation path used for CPUs with an FPU in 86Box.

It is intentionally not about the SoftFloat x87 path except where comparison is useful.

Goals:

- map the regular x87 execution paths in the interpreter and new dynarec
- determine whether regular x87 falls back to helper/interpreter code
- identify realistic ways to improve performance without regressing current behavior
- estimate when x87 work is likely to matter for overall emulator performance

Important limitation:

- this is based on code inspection, not measured host profiling or guest benchmark runs on this branch

## Executive Summary

The regular x87 path is already split into three tiers:

1. interpreter handlers in `src/cpu/x87_ops*.h`
2. direct new-dynarec recompilation for many x87 opcodes in `src/codegen_new/codegen_ops*.c`
3. helper-call fallback from compiled blocks for x87 opcodes that do not have direct recompilation coverage

That means the answer to "does regular x87 fall back to the interpreter?" is:

- yes, sometimes
- no, not always
- and in the new dynarec it is usually a per-instruction helper call from inside a compiled block, not a full return to an interpreter-only CPU execution mode

The highest-value conclusion is that regular x87 performance work should focus first on reducing helper-call fallback for common and semantically simple x87 opcodes, not on rewriting the whole x87 subsystem.

The second major conclusion is that the current regular x87 implementation already trades away some strict x87 fidelity for speed:

- the architectural stack values are stored as host `double`
- many operations rely on host scalar FP instructions or `libm`
- direct dynarec arithmetic does not appear to honor guest x87 rounding mode per arithmetic instruction

So "without losing accuracy" should be interpreted here as "do not regress current regular-x87 behavior and compatibility," not "upgrade regular x87 into a fully faithful 80-bit implementation."

## Current Architecture

### 1. Opcode table selection

Regular x87 and SoftFloat x87 are selected centrally in `src/cpu/cpu.c`.

For CPUs with an FPU:

- regular x87 interpreter tables are assigned to `x86_opcodes_d8..df_*`
- regular x87 dynarec helper tables are assigned to `x86_dynarec_opcodes_d8..df_*`
- SoftFloat uses separate opcode tables
- SoftFloat also disables direct recompilation by making `recomp_op_table = NULL` later in the new dynarec

Relevant code:

- `src/cpu/cpu.c:607-713`

This matters because the new dynarec still distinguishes:

- which interpreter/helper function table to use
- whether a direct recompilation table exists at all

### 2. Interpreter dispatch

The x87 escape opcodes dispatch through the standard opcode tables:

- `opESCAPE_d8` through `opESCAPE_df`
- then into `x86_opcodes_d8..df_*`

Relevant code:

- `src/cpu/x86_ops_fpu.h`

At this layer there is no special magic for performance. It is ordinary handler dispatch.

### 3. New dynarec x87 dispatch

The new dynarec recognizes `D8` through `DF`, marks the block as containing FPU work, and selects:

- `x86_dynarec_opcodes_d8..df_*` as the helper table
- `recomp_opcodes_d8..df` as the direct recompilation table for regular x87 only

Relevant code:

- `src/codegen_new/codegen.c:517-617`

When direct recompilation exists, the dynarec emits native FP uops.
When it does not, the dynarec emits `uop_CALL_INSTRUCTION_FUNC`.

Relevant code:

- `src/codegen_new/codegen.c:743-864`
- `src/codegen_new/codegen_backend_x86-64_uops.c:220-229`
- `src/codegen_new/codegen_backend_arm64_uops.c:219-224`

That helper call only exits the block if the helper reports a non-zero result.

## What the Regular x87 Interpreter Really Is

### 1. Stack representation

The regular x87 stack values live in `cpu_state.ST[8]` as `double`.

Relevant code:

- `src/cpu/x87_ops.h`

Push and pop are simple stack manipulations over `double`, plus tag updates:

- `src/cpu/x87_ops.h:113-165`

This is already a major semantic compromise relative to real x87 80-bit extended precision.

### 2. 80-bit memory forms are conversions, not native storage

80-bit loads and stores convert between an 80-bit memory format and host `double`:

- `x87_ld80()`
- `x87_st80()`

Relevant code:

- `src/cpu/x87_ops.h:260-294`
- `src/cpu/x87_ops_loadstore.h:429-490`

So even where the guest instruction format is 80-bit, the live execution state is still centered on host `double`.

### 3. Compare behavior is already compatibility-sensitive

`x87_compare()` and `x87_ucompare()` contain:

- inline x87 host asm on x86/x86-64 when available
- comments stating the generic C version is known to be wrong in some cases
- explicit hacks for known compatibility cases like CHKCOP and older FPU infinity behavior

Relevant code:

- `src/cpu/x87_ops.h:382-456`

This is important because any performance work that touches compare behavior is likely to be high-risk.

### 4. Arithmetic uses host FP and some expensive rounding control

The interpreter regular x87 arithmetic layer is mostly host FP over `double`.

`FADD` is special-cased to apply the guest rounding mode:

- x86/x86-64 path uses `_MM_SET_ROUNDING_MODE`
- generic path uses `fesetround()`

Relevant code:

- `src/cpu/x87_ops_arith.h:1-45`

But the larger regular x87 interpreter is not uniformly strict about host rounding control for every operation. It is best thought of as a practical approximation with a few targeted fixes.

### 5. Transcendentals and special operations are mostly `libm`

Examples:

- `F2XM1` uses `pow`
- `FYL2X` uses `log()/log(2.0)`
- `FYL2XP1` uses `log1p()/log(2.0)`
- `FPTAN` uses `tan`
- `FPATAN` uses `atan2`
- `FSQRT` uses `sqrt`
- `FSINCOS` uses `sin` and `cos`
- `FSIN` uses `sin`
- `FCOS` uses `cos`
- `FSCALE` uses `pow(2.0, ...)`

Relevant code:

- `src/cpu/x87_ops_misc.h:696-970`

These ops are correctness-sensitive and already relatively expensive. They are not good first targets for aggressive optimization unless measurement proves they dominate.

### 6. Packed BCD and other legacy formats are helper-heavy

Packed BCD load and store are manual loops with repeated digit extraction and memory traffic:

- `FBLD`
- `FBSTP`

Relevant code:

- `src/cpu/x87_ops_loadstore.h:148-220`
- `src/cpu/x87_ops_loadstore.h:231-280`

These are classic "rare but slow" helpers.

## What the New Dynarec Covers Directly

The direct handlers are in:

- `src/codegen_new/codegen_ops_fpu_arith.c`
- `src/codegen_new/codegen_ops_fpu_loadstore.c`
- `src/codegen_new/codegen_ops_fpu_misc.c`

Those handlers are lightweight. They mostly:

- emit `uop_FP_ENTER`
- emit native FP uops such as `uop_FADD`, `uop_FDIV`, `uop_FMUL`, `uop_FSUB`
- update tags
- adjust `TOP` using `fpu_PUSH`, `fpu_POP`, or `fpu_POP2`

Relevant code:

- `src/codegen_new/codegen_ops_fpu_arith.c:21-120`
- `src/codegen_new/codegen_ops_fpu_loadstore.c:21-240`
- `src/codegen_new/codegen_ops_fpu_misc.c:21-120`
- `src/codegen_new/codegen_ops_helpers.h:48-70`

On the backends, these map directly to host instructions:

- x86-64 uses `ADDSD`, `DIVSD`, `MULSD`, `SUBSD`, `COMISD`, `SQRTSD`
- ARM64 uses `FADD_D`, `FDIV_D`, `FMUL_D`, `FSUB_D`, `FCMP_D`, `FSQRT_D`

Relevant code:

- `src/codegen_new/codegen_backend_x86-64_uops.c:696-806`
- `src/codegen_new/codegen_backend_arm64_uops.c:666-756`

This is real native-FP execution, not interpreter emulation in disguise.

## Where Regular x87 Still Falls Back

I inspected the direct recompilation tables in `src/codegen_new/codegen_ops.c`.

Approximate direct coverage by escape family:

- `D8`: 100%
- `D9`: about 49%
- `DA`: about 75%
- `DB`: about 28%
- `DC`: 100%
- `DD`: about 53%
- `DE`: about 94%
- `DF`: about 48%

Relevant table definitions:

- `src/codegen_new/codegen_ops.c:282-329`
- `src/codegen_new/codegen_ops.c:379-425`
- `src/codegen_new/codegen_ops.c:475-617`

### Strongly covered areas

These are already in good shape:

- single-precision memory arithmetic (`D8`)
- double-precision memory arithmetic (`DC`)
- most register-register arithmetic
- most word integer arithmetic (`DE`)
- common dword integer arithmetic (`DA`)
- common integer load/store paths such as `FILD`, `FIST`, `FISTP`

### Weakly covered areas

The gaps are concentrated in:

- `D9` special/unary/stack/control space
- `DB` oddball integer/control space
- `DD` environment, 80-bit memory forms, and some register/control forms
- `DF` additional integer/control/status space

The missing families align with:

- transcendental and special x87 instructions
- environment save/restore
- control-word and environment loads/stores
- packed BCD
- 80-bit memory formats

## Important Architectural Tension: Rounding and Fidelity

One of the most important findings is that the regular x87 dynarec already prioritizes speed over strict x87 arithmetic fidelity.

### Interpreter side

The interpreter explicitly changes host rounding mode for some operations:

- `FADD` via `_MM_SET_ROUNDING_MODE` or `fesetround`
- various rounding helpers in `x87_ops_misc.h`

### Dynarec side

The dynarec backends store guest FP control state:

- `codegen_set_rounding_mode()` updates `cpu_state.new_fp_control`

Relevant code:

- `src/codegen_new/codegen_backend_x86-64.c:344-347`
- `src/codegen_new/codegen_backend_arm64.c:335-340`

But direct arithmetic uops do not switch rounding mode around every arithmetic instruction.
They just issue host arithmetic instructions directly.

By contrast, conversion uops do reload FP control state around integer conversions:

- `MOV_INT_DOUBLE`
- `MOV_INT_DOUBLE_64`

Relevant code:

- `src/codegen_new/codegen_backend_x86-64_uops.c:1431-1468`

So the current design is already saying:

- arithmetic speed matters more than exact guest rounding semantics for direct regular-x87 dynarec arithmetic
- integer conversion behavior is important enough to pay explicit control-state overhead

That means a future optimization effort should avoid pretending that regular x87 is currently "strictly accurate." It is already a pragmatic mode.

## Compatibility Evidence: Why x87 Work Must Be Careful

There is recent history showing that x87 performance work can break real software.

### 1. Disabled `D9 44` recompilation

There is a deliberate dynarec skip for `D9 44` because Blood II gameplay music breaks with direct recompilation.

Relevant code and history:

- `src/codegen_new/codegen_ops.c:284`
- commit `430eda39f`: "Temporarily disable the recompilation of FPU opcode D9 44"

### 2. Final Reality fixes

There were targeted x87 fixes for Final Reality both in the interpreter and the dynarec.

Relevant history:

- commit `6d3816df6`: interpreter fix
- commit `03dd94f36`: dynarec fix

Those changes revolved around keeping additional x87/MM shadow state synchronized, which is a warning sign that apparently small x87 optimizations can have surprising visual or media regressions.

### 3. MM/80-bit shadow state matters

The codebase carries extra state such as:

- `MM[].q`
- `MM_w4[]`
- `TAG_UINT64`

That shadow state is involved in:

- integer load/store special cases
- `FRSTOR`
- MMX interactions
- some historical correctness fixes

Relevant code:

- `src/cpu/x87.h`
- `src/cpu/x87_ops.h:317-358`
- `src/cpu/x87_ops_misc.h:122-214`

This makes broad "just use host doubles more aggressively" changes riskier than they first appear.

## Where Performance Work Is Most Likely To Pay Off

## Priority 1: Measure x87 helper fallback frequency first

The new dynarec already has fallback-family observability under `NEW_DYNAREC_DEVTOOLS`.

It can distinguish:

- base fallback
- `0F` fallback
- x87 fallback
- REP fallback
- 3DNow fallback

Relevant code:

- `src/codegen_new/codegen.c:71-86`
- `src/codegen_new/codegen_observability.c:509-532`

This is the right first step because x87 optimization only matters if real target workloads show meaningful x87 fallback volume.

### Why this matters

If the target workload is dominated by:

- integer core execution
- memory system overhead
- graphics emulation
- device models

then x87 work may be low leverage.

If the target workload shows a large `x87` fallback bucket, then this document's coverage recommendations become much more important.

## Priority 2: Add direct coverage for common and simple missing x87 ops

The best likely wins are missing ops that are:

- common in real software
- semantically simple
- already close to existing direct handler patterns

Best candidates:

- simple `D9` stack/control/unary ops that do not require complex environment semantics
- missing `FFREEP`-style or lightweight status/control patterns if measurement shows them
- additional low-risk load/store variants where the direct path already has almost all supporting machinery

Poor first candidates:

- `FSIN`, `FCOS`, `FPTAN`, `FPATAN`, `FYL2X`, `F2XM1`
- `FSAVE`, `FRSTOR`, `FLDENV`, `FSTENV`
- packed BCD
- anything involving complicated 80-bit reconstruction policy

## Priority 3: Optimize helper-heavy conversion paths if they are proven hot

If measurement shows a workload frequently bouncing through:

- `FIST`
- `FISTP`
- `FILD`
- 64-bit integer conversion variants

then conversion overhead may matter.

The main reason is that conversions are one of the few places where the regular dynarec still pays explicit FP-control overhead on x86-64.

This is narrower and lower-risk than touching transcendental math.

## Priority 4: Improve helper-call efficiency only after coverage work

If x87 fallback remains significant after additional direct coverage, the next tier is helper-path cost itself:

- reducing repeated effective-address setup when practical
- reducing repeated segment-limit and memory helper overhead for some x87 mem forms
- tightening specific legacy helper loops such as packed BCD

This is less attractive than direct coverage because helper calls still pay:

- the C call boundary
- interpreter-style state setup
- more conservative control flow

## Areas That Are Probably Not Worth Early Attention

### 1. Rewriting regular x87 into true 80-bit execution

That is a different project.
It would be far more invasive, and it would likely reduce performance before it improved it.

### 2. Micro-optimizing already-direct arithmetic

`FADD`, `FSUB`, `FMUL`, `FDIV`, `FCOM`, and `FSQRT` are already compiled to native host scalar FP.

There may be tiny backend wins, but these are unlikely to beat direct-coverage expansion unless profiling proves otherwise.

### 3. Aggressively changing compare semantics

Compare code already contains compatibility hacks.
This is a good place to avoid speculative cleanup.

## Expected Impact On Overall System Performance

Regular x87 optimization is unlikely to produce broad system-wide gains unless the workload is genuinely x87-heavy.

Most likely to benefit:

- x87-heavy synthetic benchmarks
- classic 3D demos and multimedia benchmarks
- CAD/scientific or math-heavy legacy applications
- games or middleware that still rely on x87 for audio, geometry, or transform work

Less likely to benefit dramatically:

- general OS boot and desktop interaction
- workloads dominated by integer code
- workloads limited by video emulation, PCI/device modeling, or memory behavior

So the expected payoff should be framed as:

- potentially meaningful for certain benchmark and game classes
- not automatically a top system-performance lever across the emulator as a whole

## Recommended Measurement Plan

Before any x87 optimization work lands, use this sequence:

1. Build with `NEW_DYNAREC_DEVTOOLS`.
2. Run representative guest workloads, not just synthetic micro-tests.
3. Record:
   - overall helper-call fallback count
   - fallback-family summary, especially the `x87` bucket
   - which concrete workloads show the highest x87 fallback pressure
4. Only if `x87` is materially large:
   - add exact x87 opcode-family or per-opcode fallback counters
   - rank missing `D9`, `DB`, `DD`, `DF` coverage by actual hit count
5. Implement direct coverage only for the top low-risk opcodes.
6. Re-test compatibility against software known to be sensitive to x87 behavior.

Recommended guest validation categories:

- one x87-heavy benchmark or demo
- one game or application with known historical x87 sensitivity
- one general Windows workload to ensure no system-level regressions

## Practical Recommendations

If this becomes active implementation work later, the order should be:

1. Measure x87 fallback usage on target workloads.
2. Add more exact x87 observability if the fallback bucket is non-trivial.
3. Land small direct-coverage additions for simple, frequently hit x87 ops.
4. Re-check compatibility after each small batch.
5. Only then consider helper-path optimization or more complex x87 semantics work.

That order keeps the work aligned with the actual architecture:

- regular x87 is already "fast where covered"
- uncovered x87 is where the obvious overhead remains
- compatibility risk rises sharply once changes touch compare behavior, 80-bit formats, environment save/restore, or transcendental semantics

## Bottom Line

Regular x87 in 86Box is not universally stuck on an interpreter fallback.
It already has a substantial native fast path in the new dynarec.

The best performance opportunity is therefore not a wholesale x87 rewrite.
It is selective expansion of direct coverage for the missing regular-x87 opcode families that are both:

- common enough to matter
- simple enough to implement without reopening old compatibility bugs

The right next step is measurement, not implementation.
