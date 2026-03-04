# Status

## Current Phase: Phase 4 retry or Phase 2 next (Phase 1+3 COMPLETE, Phase 4 REVERTED)

## Phase 1 Progress

### Bug Fixes (3/4 DONE — 1 REVERTED)

| ID | File | Status | Commit | Description |
|----|------|--------|--------|-------------|
| BUG-1 | `codegen_ir_defs.h:533` | REVERTED | 07fd7fd0a | `is_a16` double-clear — fix caused regression, see Known Issues |
| BUG-2 | `codegen.c:614` | FIXED | f67fab199 | `jump_cycles` return value — now captured |
| BUG-3 | `codegen_backend_arm64_uops.c:1879` | FIXED | 3684c49d7 | PFRSQRT — FMOV_S_ONE now writes to dest_reg |
| BUG-4 | `codegen_backend_arm64.c:344` | FIXED | 3684c49d7 | V8-V15 callee-saved — STP/LDP added in prologue/epilogue |

### ARM64 Backend Quick Fixes (1/2 DONE)

| Fix | Status | Commit | Notes |
|-----|--------|--------|-------|
| Narrow I-cache invalidation | DONE | 3684c49d7 | `codegen_allocator_clean_blocks` now takes `last_block_size` |
| W^X compliance (`pthread_jit_write_protect_np`) | NOT STARTED | — | Deferred, not blocking |

### Quick-Win Instruction Handlers (7/7 DONE)

| Instruction | Status | Commit | Notes |
|-------------|--------|--------|-------|
| LAHF (9F) | DONE | 87c56036d | `CALL_FUNC` → `helper_LAHF` (flags_rebuild + AH load) |
| SAHF (9E) | DONE | 87c56036d | `CALL_FUNC` → `helper_SAHF` (AH → flags) |
| BSWAP (0F C8-CF) | DONE | 87c56036d | Shift/OR UOPs on IREG_32, all 8 register variants |
| EMMS (0F 77) | DONE | 87c56036d | `uop_MMX_ENTER` + `CALL_FUNC` → `x87_emms` |
| FPU constants (D9 E9-ED) | DONE | 87c56036d | 5 CALL_FUNC helpers pushing to `cpu_state.ST[]` |
| FLDCW (D9 /5) | DONE | 87c56036d | MEM_LOAD_REG to IREG_NPXC + CALL_FUNC for rounding |
| SETcc (0F 90-9F) | DONE | 87c56036d | 16 wrappers + shared common via CALL_FUNC_RESULT |

### Known Issues

1. **is_a16 backend bug (BUG-1 REVERTED)**: The ARM64 and x86-64 backends both apply
   the 16-bit address mask (`& 0xffff`) AFTER adding the segment base, truncating
   the entire linear address instead of just the offset. The correct fix is:
   - Mask the offset (addr_reg + imm) to 16 bits FIRST
   - THEN add the segment base
   - Both `codegen_backend_arm64_uops.c:936` and `codegen_backend_x86-64_uops.c:1008` need fixing
   - Until fixed, the double-clear in `codegen_ir_defs.h` keeps is_a16 disabled
   - 16-bit address mode instructions fall through to the interpreter (correct but slow)
   - Regression manifested as Banshee/V3 "Unknown pixel format 00000005" crash

### Key Lessons Learned

1. **Previous agent attempts failed** because:
   - `IREG_flags_B` doesn't exist — LAHF/SAHF must use `uop_CALL_FUNC` with a C helper
   - `cpu_state.ST_i64` doesn't exist — FPU constants must use `cpu_state.ST[i]` (double)
   - Handlers were implemented but **never wired into dispatch tables** in `codegen_ops.c`

2. **Dispatch table locations** (in `codegen_ops.c`):
   - LAHF/SAHF: main opcode table positions 0x9E/0x9F (both 16-bit and 32-bit sections)
   - EMMS: `recomp_opcodes_0f` position 0x77 (both sections)
   - BSWAP: `recomp_opcodes_0f` positions 0xC8-0xCF (both sections)
   - FPU constants: `recomp_opcodes_d9` row 0xE0 positions E9-ED (both sections)
   - FLDCW: `recomp_opcodes_d9` row 0x20 positions 0x28-0x2F (+ rows 0x60, 0xA0)
   - SETcc: `recomp_opcodes_0f` positions 0x90-0x9F (both sections)

3. **ARM64 must target ARMv8.0** (baseline A64) — no v8.1+ features

4. **Working patterns for handlers**:
   - Simple integer constants: `uop_MOV_IMM` + `uop_MOV_DOUBLE_INT` (see ropFLD1/ropFLDZ)
   - Transcendental constants: `uop_CALL_FUNC(ir, helper_fn)` where helper writes `cpu_state.ST[]`
   - Flag access: `uop_CALL_FUNC(ir, flags_rebuild)` then operate on `IREG_flags` (16-bit)
   - Tag registers: `IREG_tag(n)` where n=0..7, uses `cpu_state.TOP` at compile time

5. **Always test on a Banshee/V3 VM** — it exercises more code paths during boot than simple VGA

## Audit Status (Phase 0 — COMPLETE)

- [x] Other dynarec research → `research/other-dynarecs.md`
- [x] Instruction coverage audit → `research/instruction-coverage.md`
- [x] Correctness audit → `research/correctness-audit.md`
- [x] ARM64 backend audit → `research/arm64-backend-audit.md`
- [x] UOP catalog → `research/uop-catalog.md`
- [x] Prior work review → `research/prior-work.md`
- [x] Phase roadmap synthesized → `PHASES.md`

## Branch

`cpu-dynarec-improvements` — pushed to origin

## Commits (Phase 1)

1. `f67fab199` — Fix jump_cycles dead code (BUG-2) + is_a16 double-clear (BUG-1, later reverted)
2. `3684c49d7` — Fix ARM64 dynarec bugs and add backend improvements (BUG-3 + BUG-4 + I-cache)
3. `87c56036d` — Add Phase 1 quick-win instruction handlers (LAHF/SAHF/BSWAP/EMMS/FPU constants/FLDCW/SETcc)
4. `07fd7fd0a` — Revert is_a16 fix: backend applies 16-bit mask after segment base

## Phase 3 Progress — Dead Flag Elimination

### Implementation (COMPLETE)

| Task | Status | Commit | Description |
|------|--------|--------|-------------|
| Analysis (Task #1) | DONE | — | Analyzed all flag producers/consumers, understood existing DCE |
| Backward liveness pass (Task #2) | DONE | 693af2e27 | `codegen_ir_eliminate_dead_flags()` in codegen_ir.c |
| Backend updates (Task #3) | NOT NEEDED | — | Existing `UOP_INVALID` skip handles eliminated UOPs |
| clang-format | DONE | 7b8609839 | Minor formatting fixes |

### Design Summary

- Backward walk over UOP stream, tracking 4-bit flag liveness mask
- All 4 flag registers treated as a group (all must be overwritten to kill)
- Dead flag versions have REG_FLAGS_REQUIRED cleared and added to dead list
- Existing `codegen_reg_process_dead_list()` cascades to eliminate source operands
- Conservative: BARRIER + ORDER_BARRIER make all flags live
- Fires for consecutive register-only ALU instructions without memory access between them

### Key Analysis Findings

1. **Flag producers**: Every flag-setting instruction writes all 4 registers (flags_op, flags_res, flags_op1, flags_op2)
2. **Flag consumers**:
   - Branch handlers (codegen_ops_branch.c): read flags_res/op1/op2 via IR, or CALL_FUNC (BARRIER)
   - LAHF/SAHF/PUSHF: use CALL_FUNC (BARRIER) with flags_rebuild/flags_rebuild_c
   - SETcc: use CALL_FUNC_RESULT (BARRIER)
   - INC/DEC: call flags_rebuild_c (BARRIER) then overwrite all 4 flags
3. **Existing DCE**: Already eliminates per-register dead versions between barriers, but dirty_ir_regs mechanism marks intermediate versions as REQUIRED at barrier points
4. **New pass**: Clears REQUIRED on intermediate flag versions proven dead by backward analysis
5. **No backend changes needed**: Dead UOPs set to UOP_INVALID by existing process_dead_list, skipped in compile loop

### Optimization Scope

The pass eliminates flag writes between consecutive flag-setting instructions that have NO intervening:
- BARRIER (CALL_FUNC, FP_ENTER, etc.)
- ORDER_BARRIER (memory loads/stores, jumps)
- Flag register reads (branch tests, MOVZX of flag values)

Common patterns that benefit:
- `ADD EAX, EBX` / `SUB ECX, EDX` (consecutive register ALU)
- `ADD EAX, EBX` / `MOV ECX, EDX` / `XOR ESI, ESI` (ALU with non-barrier MOV gap)
- `TEST EAX, EAX` / `CMP ECX, 0` (redundant flag sets)

### Future Enhancements

- **Relaxed ORDER_BARRIER handling**: Could skip ORDER_BARRIERs within the same x86 instruction (flag writes come after memory ops), but requires proving fault recovery correctness
- **Per-flag-component analysis**: Track individual flag bits (CF, ZF, SF, OF) instead of the 4 lazy-eval registers for finer granularity
- **Cross-block analysis**: Extend liveness to block successors (requires block linking first)

## Phase 4 — Block Linking (REVERTED)

### Attempt Summary

8 commits implemented lazy block linking (387a88199..5919b02b5), then reverted in 4d5fbbb42.
The code is preserved in git history for reference.

### Root Causes of Failure

Three bugs were identified during testing:

1. **W^X violation (SIGBUS)**: `codegen_backend_patch_link` and `codegen_backend_unpatch_link`
   write to JIT code pages without calling `pthread_jit_write_protect_np(0)` first. On macOS
   ARM64, JIT pages are executable-not-writable at runtime. Fix: wrap writes in W^X toggles.

2. **No cycle guard in linked chains (hang)**: Linked blocks jump directly A→B→A→B without
   returning to the dispatcher. No JIT block checks `cycles <= 0` — that check only exists
   in the C dispatcher loop. Fix: add a cycle-check sequence before each patchable exit stub:
   ```
   LDR  W16, [CPUSTATE, #_cycles]
   CMP  W16, #0
   B.GT +8            ; short forward skip (always in range)
   B    codegen_exit_rout  ; bail if cycles exhausted
   B    target_or_exit     ; <-- patchable instruction
   ```

3. **`host_arm64_branch_set_offset` encoding bug**: This function uses `OFFSET26` (bits [25:0])
   but `B.cond` instructions use `OFFSET19` (bits [23:5]). Using `branch_set_offset` on a
   conditional branch to a far target corrupts the instruction encoding. Fix: never use
   `branch_set_offset` on conditional branches to far targets — use `B.GT +8` (fixed short
   offset) + unconditional `B` (which uses OFFSET26 correctly).

### Lessons for Next Attempt

- Test W^X compliance immediately — any write to JIT code outside recompilation needs guards
- Block linking MUST include cycle guards to prevent infinite chains
- ARM64 conditional branches (B.cond) have ±1MB range (19-bit), unconditional B has ±128MB (26-bit)
- `host_arm64_branch_set_offset` is ONLY safe for unconditional B instructions
- The `link_entry_offset` (skip prologue) design is correct — linked blocks share the caller's stack frame
- FPU blocks need special handling: linking from non-FPU to FPU block skips TOP initialization
- The research doc and core design are sound; only the ARM64 implementation had bugs

## Next Phase

Phase 3 dead flag elimination is COMPLETE. Phase 4 block linking was attempted and reverted.

**Retry plan**: `cpu-dynarec-plan/phase4-retry-plan.md` — detailed implementation plan with
all 3 bugs addressed from the start, ARM64 backend audit findings, and step-by-step tasks.

Next recommended phases:
- **Phase 4 (retry)**: Block linking with W^X guards + cycle checks + correct encoding
- **Phase 2**: More instruction coverage — IMUL, DIV, CMOVcc, BT/BS*, RCL/RCR, SHLD/SHRD
- **Phase 3b**: Relaxed ORDER_BARRIER handling within same x86 instruction