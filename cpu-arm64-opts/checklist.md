# ARM64 CPU JIT Backend Optimization Checklist

## Guards

All C-level changes guarded with `#if defined(__aarch64__) || defined(_M_ARM64)`.
JIT backend files are inherently ARM64-only — no additional guards needed.

## Phase 1: PFRSQRT Bug Fix + 3DNow! Estimates

- [x] Fix PFRSQRT register clobber bug in `codegen_backend_arm64_uops.c:1879`
- [x] Add OPCODE_FRECPE_V2S to `codegen_backend_arm64_ops.c`
- [x] Add OPCODE_FRSQRTE_V2S to `codegen_backend_arm64_ops.c`
- [x] Add host_arm64_FRECPE_V2S emitter
- [x] Add host_arm64_FRSQRTE_V2S emitter
- [x] Add OPCODE_FRECPS_V2S to `codegen_backend_arm64_ops.c`
- [x] Add OPCODE_FRSQRTS_V2S to `codegen_backend_arm64_ops.c`
- [x] Add OPCODE_FMUL_V2S to `codegen_backend_arm64_ops.c` (already existed)
- [x] Add host_arm64_FRECPS_V2S emitter (Newton-Raphson step)
- [x] Add host_arm64_FRSQRTS_V2S emitter (Newton-Raphson step)
- [x] Add host_arm64_FMUL_V2S emitter (already existed)
- [x] Rewrite codegen_PFRCP: FRECPE + FRECPS + FMUL + DUP (mandatory refinement)
- [x] Rewrite codegen_PFRSQRT: FRSQRTE + FMUL + FRSQRTS + FMUL + DUP (mandatory refinement)
- [x] **P0 FIX**: Patch PFRCP dest==src aliasing — estimate to REG_V_TEMP first (cross-validation §1.2)
- [x] **P0 FIX**: Patch PFRSQRT dest==src aliasing — use x0\*a not x0² (aliasing-audit Option B, cross-validation §1.3-1.4)
- [ ] Build + test with 3DNow! workload
- [x] Verify PFRCP accuracy ≥ 14-bit precision (AMD spec requirement) — PASS: 16.0 bits worst case at ARM minimum 8-bit initial estimate (validation.md §2.4, §3.6)
- [x] Verify PFRSQRT accuracy ≥ 15-bit precision (AMD spec requirement) — PASS (tight): 15.41 bits worst case at ARM minimum 8-bit initial estimate, +0.41 bit margin (validation.md §2.4, §3.6)
- [x] Build + test with 3DNow! workload

### Phase 1 Testing

- [x] **BUILD**: Compiles on ARM64
- [x] **RUN TEST**: Boot Windows 98 VM with AMD K6-2, verify normal operation
- ~~Create PR~~ (not doing PRs for this branch)

**Phase 1 Verdict: PASS** — All opcode encodings (FRECPE/FRSQRTE/FRECPS/FRSQRTS), Newton-Raphson math, precision margins, and ARMv8.0 baseline compliance verified correct. Both P0 aliasing bugs fixed in commit d26977069. (validation.md §2.6)

## Phase 2: PC-Relative BL for Intra-Pool Calls

- [x] Add OPCODE_BL define to `codegen_backend_arm64_ops.c`
- [x] Implement host_arm64_call_intrapool with codegen_alloc BEFORE offset capture
- [x] Replace 26 stub calls in `codegen_backend_arm64_uops.c` with call_intrapool
- [x] Replace host_arm64_jump in codegen_JMP with host_arm64_B
- [x] Verify codegen_alloc is called BEFORE offset computation (critical!)
- [x] Build + test full boot cycle
- [x] Remove dead `host_arm64_jump` function + declaration (zero callers, confirmed by cross-validation §3.5)
- [x] Measure generated code size reduction

### Phase 2 Testing

- [x] **BUILD**: Compiles on ARM64
- [x] **RUN TEST**: Boot Windows 98 VM, verify normal operation
- [x] **RUN TEST**: Run 3DMark 99 or similar workload
- ~~Create PR~~ (not doing PRs for this branch)

**Phase 2 Verdict: PASS** — OPCODE_BL encoding verified, OFFSET26 macro correct, pool size (120 MB) within BL range (+/-128 MB), codegen_alloc ordering correct, all 26 intra-pool call sites verified, all 6 external calls correctly preserved, codegen_JMP to host_arm64_B verified, dead code (host_arm64_jump) removed. (validation.md §4.11)

## Phase 3: LOAD_FUNC_ARG*_IMM Width Fix

- [x] Change LOAD_FUNC_ARG0_IMM to use host_arm64_mov_imm
- [x] Change LOAD_FUNC_ARG1_IMM to use host_arm64_mov_imm
- [x] Change LOAD_FUNC_ARG2_IMM to use host_arm64_mov_imm
- [x] Change LOAD_FUNC_ARG3_IMM to use host_arm64_mov_imm
- [x] Build + test

### Phase 3 Testing

- [x] **BUILD**: Compiles on ARM64
- [x] **RUN TEST**: Boot Windows 98 VM, verify normal operation
- ~~Create PR~~ (not doing PRs for this branch)

## Phase 4: New ARM64 Emitters — INVESTIGATED AND REJECTED

All proposed emitters were audited for concrete UOP consumers. None found.
Root causes: IR separates compute/flag-test UOPs (no ADDS fusion), BSR/BSF not
in IR (interpreter-only, no CLZ opportunity), CSEL conditions already cover all
x87 FCMP outcomes (EQ/CC/VS), no conditional increment/invert/negate patterns
in UOP handlers. See "Investigated and Rejected" section below for per-item details.

- [x] Add CSEL_NE, CSEL_GE, CSEL_GT, CSEL_LT, CSEL_LE emitters — REJECTED (only FTST/FCOM use CSEL; existing EQ/CC/VS already cover all x87 FCMP outcomes)
- [x] Add ADDS_REG / SUBS_REG emitters (flag-setting variants) — REJECTED (IR separates compute + flag-test into distinct UOPs; no peephole window)
- [x] Add ADDS_IMM / SUBS_IMM emitters — REJECTED (same reason as ADDS_REG/SUBS_REG)
- [x] Add CLZ emitter (for BSR/BSF optimization) — REJECTED (BSR/BSF not in IR, interpreter-only)
- [x] (Optional) Add CSINC / CSINV / CSNEG emitters — REJECTED (no conditional increment/invert/negate patterns in UOP handlers)
- [x] (Optional) Add MADD / MSUB emitters — REJECTED (IR doesn't expose MUL+ADD patterns; already noted in Investigated and Rejected)

**Phase 4 Verdict: REJECTED** — All proposed emitters audited for concrete UOP consumers. None found. No code changes required.

## Phase 5: C-Level Interpreter/Dispatch Optimizations

- [x] Add LIKELY/UNLIKELY to `386_dynarec.c` hot paths (10 sites: 2 loop conditions, interpreter check, block validity, abort, cpu_init, new_ne, SMI, NMI, IRQ)
- [x] Add LIKELY/UNLIKELY to `386_common.h` fastread* functions (14 sites across fastreadb, fastreadw, fastreadl, fastreadw_fetch, fastreadl_fetch)
- [ ] Add LIKELY/UNLIKELY to memory read/write macros in `386_common.h` (skipped -- ternary operator macros don't benefit from branch hints)
- [x] (Optional) Implement branchless block validation (XOR+OR pattern at both hash-table and tree-walk sites)
- [x] (Optional) Add __builtin_prefetch for block dispatch (virtual address hash, guarded by GCC/Clang check)
- [ ] (Optional) Remove redundant __builtin_available checks (macOS ARM64) (deferred)
- [x] Build + test

### Phase 5 Testing

- [x] **BUILD**: Compiles on ARM64
- [x] **RUN TEST**: Boot Windows 98 VM, verify normal operation
- ~~Create PR~~ (not doing PRs for this branch)

## Phase 6: Benchmarking

- [ ] Define benchmark workload (specific game/app + measured metric)
- [ ] Measure before/after on representative workloads
- [ ] Document results

## Audit Findings

- [ ] Investigate ADD_LSHIFT size validation (`codegen_backend_arm64_uops.c:94-98`)
- [x] X16/X17 clobber risk — CONFIRMED NON-ISSUE (IP0/IP1 scratch by design)
- [x] REG_CPUSTATE = X29 — CONFIRMED NON-ISSUE (no FP-based stack frames)
- [x] Offset range macros unsigned-only — CONFIRMED CORRECT (matches uimm12 encoding)
- [x] MOVK_IMM validation — CONFIRMED CORRECT (caller splits half-words)
- [x] in_range7_x naming — CONFIRMED CORRECT (refers to imm7 STP/LDP field)
- [x] Source TODOs at lines 1859/1877 — ADDRESSED BY Phase 1

## Refactoring Items

- [x] R1: MMX/NEON handler macro templates — DONE (35 binary + 2 unary ops consolidated, ~500 LOC savings)
- [x] R2: Comparison operation consolidation — DONE (6 compare ops folded into R1's DEFINE_MMX_BINARY_OP macro)
- [x] R3: Shift-immediate handler factory — DONE (9 shift handlers consolidated via 3 shift-specific macros, ~160 LOC savings)
- [x] R4: HOST_REG_GET boilerplate macro — ADDRESSED BY R1-R3 (the 46 handlers with simple Q-size patterns now use macros that embed the unpack; remaining handlers have complex size dispatch that macros can't help with)
- [x] R5: Load/store stub generalization — DEFERRED (load/store routines use different register sets, different operation ordering for float conversion before/after the slow-path call; merging them for ~80 LOC savings isn't worth the correctness risk to stubs that run on every JIT memory access)
- [x] R6: Exception dispatch tail call — DONE (noinline function on ARM64, guarded with #if defined(__aarch64__) || defined(_M_ARM64))
- [x] R7: Verify PUNPCKLDQ/ZIP1 endianness equivalence — VERIFIED CORRECT (documented in comment above PUNPCK handler block; ZIP1/ZIP2 and PUNPCKL/PUNPCKH are semantically equivalent on little-endian ARM64)

## Investigated and Rejected

- [x] ADRP+ADD for globals — REJECTED (ASLR distance unpredictable)
- [x] Pinned readlookup2 register — REJECTED (register pressure trade-off)
- [x] Constant pool with LDR literal — DEFERRED (allocator changes needed)
- [x] CCMP in JIT code — REJECTED (IR doesn't expose multi-condition patterns)
- [x] LDP for read+write lookup — REJECTED (arrays not adjacent)
- [x] UDIV/SDIV for x86 DIV — REJECTED (complex x86 semantics negate savings)
- [x] MADD/MSUB fusion — REJECTED (IR doesn't expose MUL+ADD patterns)
- [x] CSEL (more conditions) — REJECTED (only FTST/FCOM use CSEL; existing EQ/CC/VS already cover all x87 FCMP outcomes)
- [x] ADDS/SUBS fusion — REJECTED (IR separates compute + flag-test into distinct UOPs; no peephole window)
- [x] CLZ/RBIT for BSR/BSF — REJECTED (BSR/BSF not in IR, interpreter-only)
- [x] CSINC/CSINV/CSNEG — REJECTED (no conditional increment/invert/negate patterns in UOP handlers)
