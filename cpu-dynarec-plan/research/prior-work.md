# Prior Dynarec Work

## Summary

Previous dynarec improvement efforts are documented here, drawn from git history
across multiple branches and the upstream 86Box project.

## Branch: 86box-arm64-cpu (Local Fork)

This branch contains the most significant prior optimization work, focused
exclusively on the ARM64 JIT backend within the new dynarec. All changes were
limited to the backend code emission layer (`codegen_backend_arm64_*.c`) and
the interpreter dispatch loop (`386_dynarec.c`).

### Completed Phases

**Phase 1: PFRSQRT Bug Fix + 3DNow! FRECPE/FRSQRTE**
- Fixed register clobber bug in PFRSQRT (FMOV_S_ONE overwrote sqrt result)
- Replaced exact FDIV/FSQRT with FRECPE/FRSQRTE + Newton-Raphson refinement
- PFRCP: 10-30 cycles -> ~6 cycles (2-5x faster)
- PFRSQRT: 21-65 cycles -> ~6 cycles (4-10x faster, plus correctness fix)
- Mandatory Newton-Raphson because ARM only guarantees 8-bit estimate precision
  while AMD 3DNow! requires 14-15 bits

**Phase 2: PC-Relative BL for Intra-Pool Stub Calls**
- Replaced MOVZ+MOVK+BLR (3-5 instructions) with single BL instruction
- 26 intra-pool stub call sites modified
- JMP to exit changed from MOVX_IMM+BR to single B instruction
- Saves ~12 instructions per typical block (~48 bytes)
- Removed dead `host_arm64_jump` function

**Phase 3: LOAD_FUNC_ARG*_IMM Width Fix**
- Changed immediate argument loading to use `host_arm64_mov_imm` instead of
  full 64-bit immediate materialization
- Most immediates fit in MOV/MOVK patterns, saving 1-3 instructions per site

**Phase 4: New ARM64 Emitters -- REJECTED**
- Investigated: CSEL_NE/GE/GT/LT/LE, ADDS_REG/SUBS_REG, CLZ, CSINC/CSINV,
  MADD/MSUB
- All rejected because no UOP consumers exist in the IR layer
- Root cause: The IR decomposes x86 into simple micro-ops where each UOP does
  exactly one thing (compute OR flag-test, never both). Backend-only
  optimizations like fused ADDS are impossible without IR-level changes.

**Phase 5: LIKELY/UNLIKELY Branch Hints**
- Added `LIKELY()`/`UNLIKELY()` macros to interpreter hot loop branches
- Minimal code change, expected benefit primarily on in-order ARM cores
  (Cortex-A53/A55) where branch prediction hints affect prefetch

### Refactoring (R1-R7)

- **R1-R3**: Consolidated 46 repetitive MMX/3DNow!/NEON handler functions into
  5 parametric macro families. Net savings: ~710 lines, identical object code.
- **R4**: Addressed by R1-R3 (the remaining ~50 handlers have complex
  size-dependent dispatch logic that cannot be macro-ized).
- **R5**: Deferred -- load/store stub generalization not worth the risk for
  ~80 LOC savings.
- **R6**: Extracted exception dispatch from hot loop to `noinline` function
  (ARM64-only). Reduces I-cache pressure in block dispatch.
- **R7**: Verified PUNPCKLDQ/ZIP1 endianness correctness on little-endian ARM64.
  No code change needed.

### Key Finding

The most impactful finding from this work is that **backend-only optimizations
have limited upside** because the IR layer prevents the patterns needed for
instruction fusion (e.g., fused compare+branch, multiply-accumulate). Meaningful
further improvement requires changes to the IR pipeline and UOP generation.

## Upstream 86Box Commits (Relevant to Dynarec)

| Commit | Description |
|--------|-------------|
| `1488097c7` | Re-enable MMX opcodes on ARM new dynarec |
| `a44ad7e77` | Remove 32-bit core dynarec (cleanup) |
| `a633bb40d` | Fix FCHS recompiling for x64 dynarecs |
| `ddea070fa` | Fix cycle period of dynarec |
| `2b3be4140` | Implement alpha planes for x64 Voodoo dynarec |
| `1e052e5dc` | Use plat_mmap on the dynarecs |
| `03dd94f36` | Fix Final Reality discolored screen for all dynarecs |
| `6bb2b447f` | Revert above (caused regression) |

### Notable Upstream Patterns

- The upstream dynarec maintenance is mostly reactive (bug fixes, regressions)
  rather than proactive optimization work.
- FCHS fix and Final Reality fix/revert show the fragility of FPU-related
  changes -- thorough testing is essential.
- MMX was disabled on ARM at some point and re-enabled, suggesting the ARM
  backend was considered less stable.

## Other Branches

No other local branches contain dynarec work beyond what is on
`86box-arm64-cpu`.

## Implications for This Project

1. **Backend-only optimization is mostly exhausted.** The ~12 instructions/block
   savings from Phase 2 (BL intra-pool) was the biggest win. Further backend
   improvements require IR-level enablement.

2. **The IR decomposition is the bottleneck.** Each x86 instruction becomes
   multiple independent UOPs with no opportunities for fusion. New UOP types
   or peephole passes could unlock backend optimizations (e.g., fused
   compare+branch, multiply-accumulate).

3. **Instruction coverage is the next frontier.** Many common instructions
   (IMUL, DIV, string ops, BSF/BSR, SHLD/SHRD, BT/BTS/BTR/BTC) fall back to
   the interpreter. Adding native UOP support for the most frequent interpreter
   fallbacks would have the highest performance impact.

4. **FPU correctness is fragile.** Both upstream (FCHS fix/revert) and local
   (PFRSQRT clobber bug) show that FPU-related changes must be tested
   extremely carefully.

5. **ARM64 target must be ARMv8.0-A baseline.** No Apple Silicon-specific
   features (though Apple Silicon can be the primary test platform).
