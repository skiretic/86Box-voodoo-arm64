# ARM64 CPU JIT Backend Optimization Checklist

## Guards

All C-level changes guarded with `#if defined(__aarch64__) || defined(_M_ARM64)`.
JIT backend files are inherently ARM64-only — no additional guards needed.

## Phase 1: PFRSQRT Bug Fix + 3DNow! Estimates

- [ ] Fix PFRSQRT register clobber bug in `codegen_backend_arm64_uops.c:1879`
- [ ] Add OPCODE_FRECPE_V2S to `codegen_backend_arm64_ops.c`
- [ ] Add OPCODE_FRSQRTE_V2S to `codegen_backend_arm64_ops.c`
- [ ] Add host_arm64_FRECPE_V2S emitter
- [ ] Add host_arm64_FRSQRTE_V2S emitter
- [ ] Add OPCODE_FRECPS_V2S to `codegen_backend_arm64_ops.c`
- [ ] Add OPCODE_FRSQRTS_V2S to `codegen_backend_arm64_ops.c`
- [ ] Add OPCODE_FMUL_V2S to `codegen_backend_arm64_ops.c`
- [ ] Add host_arm64_FRECPS_V2S emitter (Newton-Raphson step)
- [ ] Add host_arm64_FRSQRTS_V2S emitter (Newton-Raphson step)
- [ ] Add host_arm64_FMUL_V2S emitter
- [ ] Rewrite codegen_PFRCP: FRECPE + FRECPS + FMUL + DUP (mandatory refinement)
- [ ] Rewrite codegen_PFRSQRT: FRSQRTE + FMUL + FRSQRTS + FMUL + DUP (mandatory refinement)
- [ ] Build + test with 3DNow! workload
- [ ] Verify PFRCP accuracy ≥ 14-bit precision (AMD spec requirement)
- [ ] Verify PFRSQRT accuracy ≥ 15-bit precision (AMD spec requirement)

### Phase 1 Testing

- [ ] **BUILD**: Compiles on ARM64
- [ ] **RUN TEST**: Boot Windows 98 VM with AMD K6-2, verify normal operation
- [ ] Create PR for Phase 1

## Phase 2: PC-Relative BL for Intra-Pool Calls

- [ ] Add OPCODE_BL define to `codegen_backend_arm64_ops.c`
- [ ] Implement host_arm64_call_intrapool with codegen_alloc BEFORE offset capture
- [ ] Replace 23 stub calls in `codegen_backend_arm64_uops.c` with call_intrapool
- [ ] Replace host_arm64_jump in codegen_JMP with host_arm64_B
- [ ] Verify codegen_alloc is called BEFORE offset computation (critical!)
- [ ] Build + test full boot cycle
- [ ] Measure generated code size reduction

### Phase 2 Testing

- [ ] **BUILD**: Compiles on ARM64
- [ ] **RUN TEST**: Boot Windows 98 VM, verify normal operation
- [ ] **RUN TEST**: Run 3DMark 99 or similar workload
- [ ] Create PR for Phase 2

## Phase 3: LOAD_FUNC_ARG*_IMM Width Fix

- [ ] Change LOAD_FUNC_ARG0_IMM to use host_arm64_mov_imm
- [ ] Change LOAD_FUNC_ARG1_IMM to use host_arm64_mov_imm
- [ ] Change LOAD_FUNC_ARG2_IMM to use host_arm64_mov_imm
- [ ] Change LOAD_FUNC_ARG3_IMM to use host_arm64_mov_imm
- [ ] Build + test

### Phase 3 Testing

- [ ] **BUILD**: Compiles on ARM64
- [ ] **RUN TEST**: Boot Windows 98 VM, verify normal operation
- [ ] Create PR for Phase 3

## Phase 4: New ARM64 Emitters

- [ ] Add CSEL_NE, CSEL_GE, CSEL_GT, CSEL_LT, CSEL_LE emitters
- [ ] Add ADDS_REG / SUBS_REG emitters (flag-setting variants)
- [ ] Add ADDS_IMM / SUBS_IMM emitters
- [ ] Add CLZ emitter (for BSR/BSF optimization)
- [ ] (Optional) Add CSINC / CSINV / CSNEG emitters
- [ ] (Optional) Add MADD / MSUB emitters
- [ ] Build + test

### Phase 4 Testing

- [ ] **BUILD**: Compiles on ARM64
- [ ] **RUN TEST**: Boot Windows 98 VM, verify normal operation
- [ ] Create PR for Phase 4

## Phase 5: C-Level Interpreter/Dispatch Optimizations

- [ ] Add LIKELY/UNLIKELY to `386_dynarec.c` hot paths (6-8 sites)
- [ ] Add LIKELY/UNLIKELY to `386_common.h` fastreadl_fetch (2-3 sites)
- [ ] Add LIKELY/UNLIKELY to memory read/write macros in `386_common.h`
- [ ] (Optional) Implement branchless block validation
- [ ] (Optional) Add __builtin_prefetch for block dispatch
- [ ] (Optional) Remove redundant __builtin_available checks (macOS ARM64)
- [ ] Build + test

### Phase 5 Testing

- [ ] **BUILD**: Compiles on ARM64
- [ ] **RUN TEST**: Boot Windows 98 VM, verify normal operation
- [ ] Create PR for Phase 5

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

- [ ] R1: MMX/NEON handler macro templates (~400 LOC savings)
- [ ] R2: Comparison operation consolidation (~150 LOC savings)
- [ ] R3: Shift-immediate handler factory (~150-200 LOC savings)
- [ ] R4: HOST_REG_GET boilerplate macro (~200 LOC savings)
- [ ] R5: Load/store stub generalization (~80 LOC savings)
- [ ] R6: Exception dispatch tail call (I-cache improvement)
- [ ] R7: Verify PUNPCKLDQ/ZIP1 endianness equivalence

## Investigated and Rejected

- [x] ADRP+ADD for globals — REJECTED (ASLR distance unpredictable)
- [x] Pinned readlookup2 register — REJECTED (register pressure trade-off)
- [x] Constant pool with LDR literal — DEFERRED (allocator changes needed)
- [x] CCMP in JIT code — REJECTED (IR doesn't expose multi-condition patterns)
- [x] LDP for read+write lookup — REJECTED (arrays not adjacent)
- [x] UDIV/SDIV for x86 DIV — REJECTED (complex x86 semantics negate savings)
- [x] MADD/MSUB fusion — REJECTED (IR doesn't expose MUL+ADD patterns)
