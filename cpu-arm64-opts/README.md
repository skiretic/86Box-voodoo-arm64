# ARM64 CPU JIT Backend Optimizations

## Why This Work Exists

86Box's NEW_DYNAREC dynamic recompiler translates guest x86 instructions into
host machine code at runtime. On ARM64, the generated code works correctly but
contains significant inefficiencies inherited from the initial port. This
optimization effort addresses three categories of problems:

### 1. Correctness Bug (Phase 1)

The ARM64 backend's `codegen_PFRSQRT` handler — which emulates AMD 3DNow!'s
reciprocal square root estimate — had a **register clobber bug** that produced
wrong results for every PFRSQRT operation. Any emulated AMD K6-2 or K6-III
running 3DNow!-enabled software (Unreal, Quake III Arena, and other late-90s
3D titles) would get incorrect lighting, normalization, and distance
calculations.

The fix replaced the broken FSQRT+FDIV sequence with ARM64's native
FRSQRTE+Newton-Raphson pipeline, which is both correct and 4-10x faster.

### 2. Code Size Bloat (Phases 2-3)

The ARM64 backend used worst-case instruction sequences everywhere:

- **Function calls** used a 3-5 instruction sequence (materialize 64-bit
  address + BLR) even when all targets were within the same 120MB JIT memory
  pool. A single BL instruction (PC-relative, +/-128MB range) suffices.

- **Immediate loads** used a 4-instruction 64-bit sequence (MOVZ + 3x MOVK)
  even when loading small constants that fit in 1-2 instructions.

These inefficiencies compound. A typical recompiled block with 3 memory
operations wastes ~12 instructions (~48 bytes) on call overhead alone. With
960-byte block allocations, this bloat increases block chaining frequency and
reduces I-cache utilization — especially harmful on cores with small I-caches
(32KB on Cortex-A53 vs ~192KB on Apple Silicon).

### 3. Missed Compiler Hints (Phase 5, planned)

The C-level interpreter and dispatch code lacks branch prediction hints
(`LIKELY`/`UNLIKELY`). On in-order ARM64 cores (Cortex-A53/A55, common in
Chromebooks and budget ARM devices), this forces the pipeline to stall on
every mispredicted branch. Adding hints to the 6-8 hot paths in the
dispatch loop can yield 1-10% throughput improvement on these cores at
zero behavioral risk.

## What's Been Done

| Phase | Change | Impact | Status |
|-------|--------|--------|--------|
| 1 | Fix PFRSQRT clobber bug + replace FSQRT/FDIV with FRSQRTE/NR | Correctness fix + 4-10x latency reduction | Done |
| 1+ | Fix PFRCP/PFRSQRT dest==src register aliasing | Correctness fix for aliased register case | Done |
| 2 | Replace 26 stub calls with PC-relative BL | ~12 insns saved per typical block | Done |
| 2+ | Replace JMP with single B instruction, remove dead code | 3-4 insns saved per block exit | Done |
| 3 | Use mov_imm for LOAD_FUNC_ARG*_IMM | 2-3 insns saved per immediate arg load | Done |

## What's Planned

| Phase | Change | Impact | Status |
|-------|--------|--------|--------|
| 4 | Add CSEL/ADDS/SUBS/CLZ emitters | Enables branchless patterns, fused ops | Planned |
| 5 | LIKELY/UNLIKELY annotations in interpreter | 1-10% on in-order cores | Planned |
| 6 | Benchmarking | Quantify actual gains | Planned |

## Who Benefits

- **All ARM64 users**: Phases 1-3 reduce code size and fix a correctness bug.
  This affects Apple Silicon Macs, Asahi Linux, Windows on ARM, Raspberry Pi
  4/5, and any ARMv8.0+ device running 86Box.

- **Budget ARM devices disproportionately**: Cortex-A53/A55 cores (Raspberry
  Pi, Chromebooks) benefit most from code size reduction (smaller I-cache) and
  branch hints (in-order pipeline). Apple Silicon benefits less because its
  deep OOO pipeline and large caches mask many of these inefficiencies.

- **3DNow! game emulation**: The PFRSQRT fix is critical for anyone emulating
  an AMD K6-2/K6-III with 3DNow!-enabled games. Without the fix, 3D rendering
  produces visibly wrong output.

## Design Constraints

All optimizations target **ARMv8.0-A baseline** — no optional extensions, no
Apple-specific features, no FEAT_RPRES or SVE. The generated code must run
correctly on every conformant AArch64 implementation from Cortex-A53 to Apple
M4 to Graviton 4.

The JIT pool is a single contiguous 120MB mmap allocation. All stubs and
generated blocks live within this pool, guaranteeing BL's +/-128MB range
constraint is always satisfied. External C function calls (readmembl,
writemembl, etc.) continue to use absolute addressing (MOVX_IMM + BLR).

## Document Index

| File | Purpose |
|------|---------|
| `README.md` | This file — rationale and overview |
| `plan.md` | Implementation plan with phase details |
| `checklist.md` | Task tracking with completion status |
| `CHANGELOG.md` | Per-phase technical changelog |
| `validation.md` | Source of truth — all research, validation, and audit findings |
