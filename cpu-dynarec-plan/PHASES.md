# CPU Dynarec Improvement Phases

Status: **FINAL** — All audits complete, roadmap synthesized

## Audit Summary

| Audit | Agent | Key Finding |
|-------|-------|-------------|
| Other dynarecs | cpu-arch | Block linking (15-30%) and dead flag elimination (10-30%) are the two biggest wins. Every other dynarec does both; 86Box does neither. |
| Instruction coverage | cpu-x86ops | 1-byte: 69.5%, 0F 2-byte: 26.2%, 3DNow! ARM64: 0%. Critical gaps: REP string ops, IMUL 2/3-op, SETcc, CMOVcc, MUL/DIV. |
| Correctness | cpu-debug | 2 bugs (is_a16 double-clear, jump_cycles discarded). Accumulator is cycles-only, not flags. No verification infrastructure. |
| ARM64 backend | cpu-arm64 | 2 bugs (PFRSQRT wrong reg, V8-V15 not saved). Missed optimizations: direct BL, MOVN, narrow I-cache flush, single B.cond. |
| UOP catalog | cpu-lead | 145 UOPs across 17 categories. Backend optimization exhausted — must work at IR/UOP level. |

## Estimated Impact Rankings

From the research, ranked by estimated performance gain:

| Optimization | Est. Gain | Evidence |
|-------------|-----------|----------|
| Block linking / direct jump patching | 15-30% | Every other dynarec does this; 86Box returns to dispatcher every block exit |
| Dead flag elimination (Kildall's) | 10-30% | box64 measured 30% on 7zip; Rosetta 2 calls it their #1 optimization |
| Instruction coverage expansion | 5-15% | Depends on workload; REP string ops and IMUL are the hottest gaps |
| ARM64 backend code quality | 3-8% | Direct BL, MOVN, narrow I-cache — incremental but cumulative |
| Native NZCV flag mapping | 5-15% | Requires IR changes; high reward but complex |

---

## Phase 1: Bug Fixes & Quick Wins

**Effort**: Small | **Impact**: High (correctness + easy perf wins) | **Dependencies**: None

Fix all confirmed bugs and implement trivial instruction handlers that are high-value, low-effort.

### Bug Fixes (4 confirmed)

| ID | File | Issue | Severity | Fix |
|----|------|-------|----------|-----|
| BUG-1 | `codegen_ir_defs.h:533` | `is_a16` double-clear — breaks 16-bit address wrapping for LES/LDS/LFS/LGS/LSS | Medium | Remove line 533 (`uop->is_a16 = 0`) |
| BUG-2 | `codegen.c:614` | `jump_cycles` return value discarded — jump cycle timing is dead code | Low | `jump_cycles = codegen_timing_jump_cycles()` |
| BUG-3 | `codegen_backend_arm64_uops.c:1879` | PFRSQRT writes FMOV_S_ONE to REG_V_TEMP, clobbering FSQRT result | Medium | Write FMOV_S_ONE to `dest_reg` instead |
| BUG-4 | `codegen_backend_arm64.c:344` | V8-V15 callee-saved SIMD regs not saved in prologue | Medium | Add STP/LDP for V8-V15 in prologue/epilogue |

### Quick-Win Instruction Handlers

| Instruction | Opcode(s) | Complexity | Why It Matters |
|-------------|-----------|------------|----------------|
| SETcc | 0F 90-9F (16 ops) | S | Compilers emit heavily for branchless conditionals |
| BSWAP | 0F C8-CF (8 ops) | S | Trivial on ARM64 (REV instruction), used in networking/multimedia |
| LAHF | 9F | S | Critical for FPU comparison patterns (FCOM → FSTSW → SAHF → Jcc) |
| SAHF | 9E | S | Same FPU pattern — without these, entire sequences fall to interpreter |
| EMMS | 0F 77 | S | Required MMX→FPU transition, trivially JIT-able |
| FPU constants | D9 E8-EE | S | FLD1, FLDL2T, FLDL2E, FLDPI, FLDLG2, FLDLN2, FLDZ — just load immediates |
| FLDCW | D9 /5 | S | FPU control word load, used by every FP program at startup |

### ARM64 Backend Quick Fixes

| Fix | Complexity | Impact |
|-----|------------|--------|
| Narrow I-cache invalidation (flush only bytes written, not full MEM_BLOCK_SIZE) | S | Reduces I-cache pressure on Apple Silicon |
| W^X compliance: add `pthread_jit_write_protect_np()` calls | S | Future-proof for macOS hardened runtime |

**Total tasks**: ~15 | **Estimated complexity**: All S (small)

---

## Phase 2: Instruction Coverage — Core Gaps

**Effort**: Medium | **Impact**: High (5-15% for compute-heavy workloads) | **Dependencies**: Phase 1 bug fixes

Fill the most impactful instruction coverage gaps identified by the audit.

### Tier 1: Multiply/Divide (most common compiler-generated patterns)

| Instruction | Opcode | Complexity | Notes |
|-------------|--------|------------|-------|
| IMUL r, r/m (2-operand) | 0F AF | M | Most common multiply in 32-bit compiled code |
| IMUL r, r/m, imm (3-operand) | 69, 6B | M | Second most common multiply form |
| MUL r/m (1-operand) | F6/F7 /4 | M | Full-width EDX:EAX result |
| IMUL r/m (1-operand) | F6/F7 /5 | M | Signed full-width result |
| DIV r/m | F6/F7 /6 | M | Unsigned divide — complex (needs UDIV + MSUB on ARM64) |
| IDIV r/m | F6/F7 /7 | M | Signed divide |

**Note**: The F6/F7 group handler currently has `if (fetchdat & 0x20) return 0`, explicitly rejecting MUL/DIV sub-opcodes. This needs a new dispatch structure.

### Tier 2: Conditional Operations

| Instruction | Opcode(s) | Complexity | Notes |
|-------------|-----------|------------|-------|
| CMOVcc | 0F 40-4F (16 ops) | M | Conditional moves — perfect match for ARM64 CSEL |
| POPF/POPFD | 9D | M | Needed for function epilogues, flag restore patterns |

### Tier 3: Bit Operations

| Instruction | Opcode(s) | Complexity | Notes |
|-------------|-----------|------------|-------|
| BT r/m, r | 0F A3 | S | Bit test — common in bitmap/flag checking |
| BTS r/m, r | 0F AB | S | Bit test and set |
| BTR r/m, r | 0F B3 | S | Bit test and reset |
| BTC r/m, r | 0F BB | S | Bit test and complement |
| BSF r, r/m | 0F BC | S | Bit scan forward — ARM64 RBIT+CLZ |
| BSR r, r/m | 0F BD | S | Bit scan reverse — ARM64 CLZ |

### Tier 4: Remaining Shift/Rotate Gaps

| Instruction | Opcode(s) | Complexity | Notes |
|-------------|-----------|------------|-------|
| RCL r/m | C0-C1/D0-D3 /2 | M | Rotate through carry — complex flag interaction |
| RCR r/m | C0-C1/D0-D3 /3 | M | Same complexity as RCL |
| SHLD r/m, r, CL | 0F A5 | M | Only immediate-count form exists; CL variant missing |
| SHRD r/m, r, CL | 0F AD | M | Same — CL variant missing |

**Total tasks**: ~20 | **Estimated complexity**: Mix of S and M

---

## Phase 3: Dead Flag Elimination

**Effort**: Medium-Large | **Impact**: Very High (10-30% estimated) | **Dependencies**: None (can start in parallel with Phase 2)

This is the single highest-impact optimization available. Every other major dynarec implements some form of flag liveness analysis.

### Background

86Box currently stores all 4 flag components (`flags_op`, `flags_res`, `flags_op1`, `flags_op2`) after EVERY flag-setting instruction, even when the flags are never read before the next flag-setting instruction overwrites them. This is pure waste in the common case.

Evidence from other projects:
- **box64**: Measured **30% speedup on 7zip** from dead flag elimination alone
- **Rosetta 2**: Apple considers dead flag elimination their most important software optimization
- **FEX-Emu**: Full SSA-based dead flag analysis across basic blocks

### Implementation Plan

#### 3.1: Flag Liveness Analysis (Kildall's Algorithm)

| Task | Complexity | Description |
|------|------------|-------------|
| Define flag groups | S | Categorize which UOPs produce which flag subsets (CF, OF, SF, ZF, AF, PF) |
| Backward scan within block | M | Walk UOPs from end to start, tracking which flags are live (consumed before next producer) |
| Mark dead flag producers | S | Tag flag-producing UOPs whose output is never consumed |
| Skip dead flag UOPs in backend | M | Backend checks the "dead" tag and skips flag computation |

#### 3.2: Cross-Block Flag Liveness (stretch goal)

| Task | Complexity | Description |
|------|------------|-------------|
| Build block successor graph | M | Track which blocks can follow which (fall-through + jump targets) |
| Propagate flag liveness across edges | L | Iterative dataflow: if successor needs CF, predecessor must produce CF |
| Handle indirect jumps conservatively | S | Assume all flags live for indirect jumps/calls |

#### 3.3: IR Integration

| Task | Complexity | Description |
|------|------------|-------------|
| Add flag liveness pass to `codegen_ir.c` | M | Run after existing DCE pass, before backend lowering |
| Add `UOP_FLAGS_DEAD` metadata | S | New UOP annotation that backends can check |
| Update ARM64 backend to skip dead flags | M | In `codegen_backend_arm64_uops.c`, check annotation before emitting flag stores |
| Update x86-64 backend similarly | M | Same for `codegen_backend_x86-64_uops.c` |

**Total tasks**: ~10 | **Estimated complexity**: Mix of S, M, L

---

## Phase 4: Block Linking

**Effort**: Large | **Impact**: Very High (15-30% estimated) | **Dependencies**: Phase 1 (bug fixes)

86Box is the ONLY dynarec studied that returns to the C dispatcher after every block exit. Every other dynarec (box64, FEX-Emu, QEMU TCG, Rosetta 2, Dolphin, RPCS3) patches block exits to jump directly to the next block.

### Current Flow (slow)

```
Block A executes
  → branch to dispatcher (hash lookup)
  → find Block B
  → jump to Block B
Block B executes
  → branch to dispatcher (hash lookup)
  → ...
```

### Target Flow (fast)

```
Block A executes
  → direct jump to Block B (patched)
Block B executes
  → direct jump to Block C (patched)
  → ...
```

### Implementation Plan

#### 4.1: Direct Jump Patching

| Task | Complexity | Description |
|------|------------|-------------|
| Add link slots to `codeblock_t` | S | Store the address of the branch instruction(s) at block exit |
| Implement `codegen_block_link()` | M | When Block B is compiled, patch Block A's exit to jump directly to Block B |
| Handle conditional branches | M | For Jcc, patch the taken path; fall-through is already linked to next block |
| ARM64: emit patchable branch stubs | M | Emit `B dispatcher` as placeholder, record offset for later patching |
| x86-64: emit patchable branch stubs | M | Same for x86-64 backend |

#### 4.2: Block Unlinking (invalidation safety)

| Task | Complexity | Description |
|------|------------|-------------|
| Track incoming links per block | M | Each block stores a list of blocks that jump into it |
| `codegen_block_unlink()` | M | When a block is invalidated, unpatch all incoming jumps back to dispatcher |
| Handle self-modifying code | M | SMC invalidation must unlink before freeing the block |

#### 4.3: Fall-Through Chaining

| Task | Complexity | Description |
|------|------------|-------------|
| Detect sequential blocks | S | If Block B starts at the byte after Block A ends, no jump needed |
| Emit fall-through | S | Simply don't emit the exit branch — execution flows into next block |

**Total tasks**: ~10 | **Estimated complexity**: Mix of S, M, and one implicit L (the linking infrastructure)

---

## Phase 5: ARM64 Backend Optimizations

**Effort**: Medium | **Impact**: Moderate (3-8% cumulative) | **Dependencies**: Phase 1

Incremental ARM64 code generation improvements. Each is small but they compound.

### Code Generation Improvements

| Optimization | Complexity | Est. Gain | Description |
|-------------|------------|-----------|-------------|
| Direct BL for known targets | M | 2-4% | Replace MOVX_IMM (2-4 insns) + BLR with ADRP+ADD+BLR or direct BL (1 insn) for targets within ±128MB |
| MOVN optimization | S | 1-2% | Use MOVN for constants with many 0xFFFF halfwords (e.g., -1, -2, addresses near top of address space) |
| Single B.cond for short branches | M | 1-2% | Replace inverted-B.cond + unconditional-B pair with single B.cond for intra-block jumps within ±1MB |
| LDP/STP pairing | M | 1-2% | Pair adjacent register spills/reloads into single LDP/STP instructions |
| FRECPE/FRSQRTE for 3DNow! | S | <1% | Replace FDIV/FSQRT with NEON reciprocal estimate + Newton-Raphson (prior branch attempted this but changes may not be merged) |

### Prologue/Epilogue

| Optimization | Complexity | Description |
|-------------|------------|-------------|
| Minimize saved registers | S | Audit which callee-saved registers are actually used and skip saving unused ones |
| Frame pointer elimination | S | If not needed for unwinding, skip FP setup |

**Total tasks**: ~7 | **Estimated complexity**: Mix of S and M

---

## Phase 6: REP String Operations

**Effort**: Large | **Impact**: High for specific workloads (memcpy/memset patterns) | **Dependencies**: Phase 2 (instruction coverage infrastructure)

The REP prefix currently forces `recomp_op_table = NULL`, sending the ENTIRE instruction to the interpreter. This is the single biggest coverage gap — memcpy, memset, and string comparison patterns hit this constantly in DOS games, Windows 9x, and Windows XP.

### Implementation Plan

String ops need special IR treatment because they're variable-length loops. Two approaches:

#### Approach A: Bounded Unroll (simpler)
- Emit a small unrolled loop (e.g., 4-8 iterations) in the IR
- If ECX > unroll count, fall back to interpreter
- Good enough for short string ops (most common case)

#### Approach B: Native Loop (faster for bulk)
- Emit a native ARM64 loop with LDRB/STRB + SUBS + B.NE
- Handle page-crossing checks
- For MOVSB/W/D: Can use LDP/STP for aligned bulk copies

| Instruction | Complexity | Priority | Notes |
|-------------|------------|----------|-------|
| REP MOVSB/W/D | L | Critical | memcpy pattern — most impactful |
| REP STOSB/W/D | M | Critical | memset pattern — second most impactful |
| REP CMPSB/W/D | L | Medium | memcmp pattern |
| REP SCASB/W/D | M | Medium | strlen-like pattern |
| REPE/REPNE variants | L | Medium | Conditional repeat prefixes |

**Total tasks**: ~5-8 | **Estimated complexity**: Mostly L

---

## Phase 7: Advanced Optimizations (Future)

**Effort**: Very Large | **Impact**: Moderate-High | **Dependencies**: Phases 3-4

These are stretch goals that would bring 86Box closer to state-of-the-art dynarecs. Each is a significant project.

### 7.1: Native NZCV Flag Mapping

Map x86 flag computations directly to ARM64 condition codes where possible, avoiding the 4-register flag store entirely. This builds on Phase 3's flag liveness analysis.

- **Complexity**: L
- **Est. gain**: 5-15%
- **Risk**: High — x86 and ARM64 flags are similar but not identical (AF, PF have no ARM64 equivalent)

### 7.2: Call-Return Stack Optimization

Use ARM64 BL/RET instruction pair to leverage the hardware return address predictor. Currently, x86 CALL/RET go through the dispatcher, missing the predictor entirely.

- **Complexity**: M
- **Est. gain**: 5-10% for call-heavy code
- **Risk**: Low

### 7.3: Hot Block Profiling

Add lightweight execution counters to identify hot blocks. Prioritize optimization effort (e.g., recompile with more aggressive passes) for frequently-executed blocks.

- **Complexity**: M
- **Est. gain**: Enables other optimizations
- **Risk**: Low

### 7.4: Trace Compilation (Super-Blocks)

Compile traces across basic block boundaries, following the most common execution path. This enables cross-block register allocation and dead code elimination.

- **Complexity**: XL
- **Est. gain**: 10-25%
- **Risk**: Very high — major architectural change

### 7.5: Verification Infrastructure

Build a JIT vs interpreter comparison framework for automated correctness testing.

- **Complexity**: L
- **Est. gain**: N/A (correctness, not performance)
- **Risk**: Low
- **Note**: No verification infrastructure currently exists for the CPU dynarec

---

## Phase Summary

| Phase | Name | Effort | Impact | Tasks | Key Deliverable |
|-------|------|--------|--------|-------|-----------------|
| 1 | Bug Fixes & Quick Wins | S | High | ~15 | 4 bugs fixed, 7 new instruction handlers, ARM64 fixes |
| 2 | Instruction Coverage | M | High | ~20 | IMUL, DIV, CMOVcc, BT/BS*, RCL/RCR, SHLD/SHRD |
| 3 | Dead Flag Elimination | M-L | Very High (10-30%) | ~10 | Kildall's algorithm, flag liveness pass |
| 4 | Block Linking | L | Very High (15-30%) | ~10 | Direct jump patching, block chaining |
| 5 | ARM64 Backend Opts | M | Moderate (3-8%) | ~7 | Direct BL, MOVN, LDP/STP, narrow I-flush |
| 6 | REP String Ops | L | High (workload-dependent) | ~5-8 | REP MOVS/STOS/CMPS/SCAS JIT |
| 7 | Advanced | XL | High (10-25%) | ~5+ | NZCV mapping, call-return, traces |

### Recommended Execution Order

**Phases 1 → 3 → 4 → 2 → 5 → 6 → 7**

Rationale:
- **Phase 1 first**: Bug fixes are always first. Quick-win instructions are trivial and immediately useful.
- **Phase 3 before Phase 4**: Flag elimination is slightly less complex than block linking but similar impact. Getting flag analysis working first also simplifies block linking (fewer registers to track across block boundaries).
- **Phase 4 next**: Block linking is the other huge win and benefits all subsequent work.
- **Phase 2 after 3+4**: Instruction coverage is important but each individual instruction is a smaller win than the systemic optimizations in Phases 3-4.
- **Phase 5 after 2**: ARM64 backend tweaks are small individual gains.
- **Phase 6 after 5**: REP string ops are high-effort and workload-specific.
- **Phase 7 is aspirational**: Only pursue after Phases 1-6 are solid.

### Parallelism Opportunities

- Phases 2 and 3 can run in parallel (different codepaths)
- Phases 2 and 5 can run in parallel (x86ops agent + arm64 agent)
- Phase 7.5 (verification) can start anytime
