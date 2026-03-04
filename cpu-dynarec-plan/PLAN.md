# CPU Dynarec Improvement Plan

## Goals

Measurably improve the 86Box "new dynarec" (`src/codegen_new/`) in both
performance and correctness, with ARM64 (Apple Silicon) as the primary target.

Specific objectives:

1. **Increase JIT coverage** -- Reduce the number of x86 instructions that fall
   back to the interpreter. Every interpreter fallback requires a full barrier,
   register flush, and function call.

2. **Improve generated code quality** -- Better register allocation, fewer
   spills, smaller native code for common patterns (LEA, flag-consuming
   branches, string ops).

3. **Strengthen correctness** -- Ensure JIT-compiled code produces identical
   results to the interpreter for all supported x86 instructions.

4. **Optimize the IR pipeline** -- Add optimization passes (dead code
   elimination improvements, constant folding, peephole) to the IR layer
   before backend lowering.

5. **Improve ARM64 backend** -- Leverage ARM64-specific instructions (CSEL,
   MADD, UBFM/SBFM, conditional compares) where the IR provides the
   opportunity.

## Non-Goals

- **Not a rewrite.** The existing three-stage pipeline (x86 decode -> UOPs ->
  IR optimization -> backend lowering) is sound. We improve it, not replace it.

- **Not adding new ISA extensions** beyond what's needed. We target ARMv8.0-A
  baseline for maximum compatibility (Cortex-A53 through Apple M-series).

- **Not targeting other platforms.** RISC-V, MIPS, etc. are out of scope.
  x86-64 backend improvements are welcome as side effects but not the focus.

- **Not cycle-accurate timing.** The CPU timing models
  (`codegen_timing_*.c`) are a separate concern and not part of this plan.

## Architecture Overview

The new dynarec has a three-stage pipeline:

```
x86 instruction stream
        |
        v
+-------------------+
| Stage 1: Decode   |  codegen_ops_*.c
| x86 -> UOPs       |  ~80 handler files translate x86 opcodes to
|                    |  platform-independent micro-operations
+-------------------+
        |
        v
+-------------------+
| Stage 2: IR Opt   |  codegen_ir.c
| Dead code elim    |  Processes UOP stream, removes dead register
| Register versions |  writes, manages register versioning
+-------------------+
        |
        v
+-------------------+
| Stage 3: Backend  |  codegen_backend_arm64_uops.c (ARM64)
| UOP -> Native     |  codegen_backend_x86-64_uops.c (x86-64)
| Register alloc    |  codegen_reg.c allocates host registers
| Code emission     |  Backend-specific emitters produce native code
+-------------------+
        |
        v
  Native code block (codeblock_t)
```

### Key Data Structures

| Structure | File | Purpose |
|-----------|------|---------|
| `uop_t` | `codegen_ir_defs.h` | Single micro-operation with type, regs, imm, pointer |
| `ir_data_t` | `codegen_ir_defs.h` | Array of up to 4096 UOPs forming a basic block |
| `codeblock_t` | `codegen.h` | Compiled native code block with metadata |
| `ir_reg_t` | `codegen_reg.h` | IR register reference (reg + version) |
| `reg_version_t` | `codegen_reg.h` | Register version tracking for dead code elimination |

### UOP Type Flags

UOPs carry metadata flags in their upper bits:

| Flag | Bit | Meaning |
|------|-----|---------|
| `UOP_TYPE_BARRIER` | 31 | Full barrier -- all regs must be written back/discarded |
| `UOP_TYPE_PARAMS_IMM` | 30 | UOP uses immediate data |
| `UOP_TYPE_PARAMS_POINTER` | 29 | UOP uses pointer field |
| `UOP_TYPE_PARAMS_REGS` | 28 | UOP uses source/dest registers |
| `UOP_TYPE_ORDER_BARRIER` | 27 | Order barrier -- regs written back but not discarded |
| `UOP_TYPE_JUMP` | 26 | UOP is a branch, target in jump_dest_uop |
| `UOP_TYPE_JUMP_DEST` | 25 | UOP is a branch target |

### IR Register File

The IR uses a virtual register file (`codegen_reg.h`) that maps to the
emulated x86 state:

- **IREG_EAX..IREG_EDI** (0-7): General-purpose registers
- **IREG_flags_op/res/op1/op2** (8-11): Lazy flag evaluation state
- **IREG_pc/oldpc** (12-13): Program counter
- **IREG_eaaddr/ea_seg** (14-15): Effective address calculation
- **IREG_ST0..ST7** (physical FPU stack): x87 FPU registers
- **IREG_MM0..MM7**: MMX registers (aliased with FPU)
- **IREG_temp0..temp3**: Temporary registers (stack-allocated, not preserved)

Registers have size annotations (L/W/B/BH/D/Q) and are versioned. The dead
code elimination pass uses refcounts on register versions to identify and
remove unused writes.

### Flag Accumulation

The accumulator (`codegen_accumulate.c`) currently handles only one register:
`ACCREG_cycles`. Cycle count decrements are accumulated and flushed as a single
operation rather than emitting individual SUB instructions per x86 instruction.

## Phase Breakdown

See `PHASES.md` for the complete phased roadmap. Summary:

| Phase | Name | Effort | Impact |
|-------|------|--------|--------|
| 1 | Bug Fixes & Quick Wins | S | High |
| 2 | Instruction Coverage — Core Gaps | M | High (5-15%) |
| 3 | Dead Flag Elimination (Kildall's) | M-L | Very High (10-30%) |
| 4 | Block Linking / Jump Patching | L | Very High (15-30%) |
| 5 | ARM64 Backend Optimizations | M | Moderate (3-8%) |
| 6 | REP String Operations | L | High (workload-dependent) |
| 7 | Advanced (NZCV mapping, traces) | XL | High (10-25%) |

**Recommended order**: 1 → 3 → 4 → 2 → 5 → 6 → 7

## Research Documents

All audit results are in `research/`:

| Document | Contents |
|----------|----------|
| `other-dynarecs.md` | box64, FEX-Emu, QEMU TCG, Rosetta 2, Dolphin, RPCS3 analysis |
| `instruction-coverage.md` | Full opcode dispatch table audit with coverage percentages |
| `correctness-audit.md` | IR, flag handling, register allocator, block boundary audit |
| `arm64-backend-audit.md` | ARM64 code generation quality, UOP coverage, NEON usage |
| `uop-catalog.md` | Complete catalog of all 145 UOP types across 17 categories |
| `prior-work.md` | Previous 86box-arm64-cpu branch optimization history |

## Key Metrics

### Coverage Metrics

1. **JIT compilation rate**: Fraction of executed x86 instructions that run as
   JIT-compiled native code vs. interpreter fallback. Measured by counting
   `UOP_CALL_INSTRUCTION_FUNC` (interpreter fallback) vs. native UOP sequences
   in compiled blocks.

2. **UOP density**: Average number of UOPs per compiled block. Higher density
   means more work per block entry overhead.

3. **Block invalidation rate**: How often compiled blocks are invalidated
   (self-modifying code, page mapping changes). Lower is better.

### Performance Metrics

1. **Native code size per block**: Bytes of ARM64 machine code generated per
   block. Smaller code = better I-cache utilization.

2. **Real workload benchmarks**: Boot time, application launch time, and
   synthetic benchmarks (e.g., DOOM timedemo, Quake timedemo, 3DMark) inside
   the emulated machine.

3. **Host CPU metrics**: Instructions per cycle (IPC), branch misprediction
   rate, I-cache miss rate -- measured with `perf` (Linux) or Instruments
   (macOS) on the host.

### Correctness Metrics

1. **Interpreter/JIT comparison**: Run identical instruction sequences through
   both paths, compare all register and flag state. Must be bit-identical.

2. **Real-world boot success**: Windows 3.1, Windows 95, Windows 98, Windows
   NT 4.0, and DOS games must boot and run without regressions.

## Prior Work

The `86box-arm64-cpu` branch contains previous ARM64 JIT backend optimizations:

- **Phase 1**: PFRSQRT bug fix + 3DNow! FRECPE/FRSQRTE (with Newton-Raphson)
- **Phase 2**: PC-relative BL for intra-pool stub calls (2-4 insns saved per call)
- **Phase 3**: LOAD_FUNC_ARG*_IMM width fix (mov_imm for immediates)
- **Phase 4**: New ARM64 emitters -- investigated and rejected (no UOP consumers)
- **Phase 5**: LIKELY/UNLIKELY branch hints in interpreter hot loop
- **R1-R7**: Code consolidation (46 MMX/3DNow! handlers to parametric macros)

Key finding from Phase 4: The IR layer decomposes x86 instructions into simple
micro-operations where each UOP does exactly one thing (compute OR flag-test,
never both). This limits the effectiveness of backend-only optimizations --
meaningful improvement requires changes at the IR/UOP level too.

See `cpu-dynarec-plan/research/prior-work.md` for details.
