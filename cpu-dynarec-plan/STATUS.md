# Status

## Current Phase: Phase 4 COMPLETE (Phases 1, 3, 4 COMPLETE)

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
| LAHF (9F) | DONE | 87c56036d | `CALL_FUNC` -> `helper_LAHF` (flags_rebuild + AH load) |
| SAHF (9E) | DONE | 87c56036d | `CALL_FUNC` -> `helper_SAHF` (AH -> flags) |
| BSWAP (0F C8-CF) | DONE | 87c56036d | Shift/OR UOPs on IREG_32, all 8 register variants |
| EMMS (0F 77) | DONE | 87c56036d | `uop_MMX_ENTER` + `CALL_FUNC` -> `x87_emms` |
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

- [x] Other dynarec research -> `research/other-dynarecs.md`
- [x] Instruction coverage audit -> `research/instruction-coverage.md`
- [x] Correctness audit -> `research/correctness-audit.md`
- [x] ARM64 backend audit -> `research/arm64-backend-audit.md`
- [x] UOP catalog -> `research/uop-catalog.md`
- [x] Prior work review -> `research/prior-work.md`
- [x] Phase roadmap synthesized -> `PHASES.md`

## Branch

`cpu-dynarec-improvements` — pushed to origin

## Commits (Phase 1)

1. `f67fab199` — Fix jump_cycles dead code (BUG-2) + is_a16 double-clear (BUG-1, later reverted)
2. `3684c49d7` — Fix ARM64 dynarec bugs and add backend improvements (BUG-3 + BUG-4 + I-cache)
3. `87c56036d` — Add Phase 1 quick-win instruction handlers (LAHF/SAHF/BSWAP/EMMS/FPU constants/FLDCW/SETcc)
4. `07fd7fd0a` — Revert is_a16 fix: backend applies 16-bit mask after segment base

## Phase 3 Progress — Dead Flag Elimination (COMPLETE)

### Implementation

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

### Optimization Scope

The pass eliminates flag writes between consecutive flag-setting instructions that have NO intervening:
- BARRIER (CALL_FUNC, FP_ENTER, etc.)
- ORDER_BARRIER (memory loads/stores, jumps)
- Flag register reads (branch tests, MOVZX of flag values)

## Phase 4 — Block Linking (COMPLETE)

### Summary

Lazy block linking with cycle-guarded exit stubs. Compiled blocks with unconditional JMP
exits can be directly patched to jump to their target block, bypassing the C dispatcher.
Tested and verified: Win98 boots to desktop on ARM64 (Apple Silicon).

### Implementation

| Component | File(s) | Description |
|-----------|---------|-------------|
| Core infrastructure | `codegen_block.c` | `codeblock_t` link fields, `try_link_exit()`, `codegen_block_unlink()`, `CODEBLOCK_NEEDS_LINKING` one-shot flag |
| IR exit tracking | `codegen_ir.c` | Stores `exit_pc` (cs + eip) and `exit_patch_offset` per block exit during IR compilation |
| ARM64 backend | `codegen_backend_arm64.c`, `codegen_backend_arm64_uops.c` | Cycle-guarded 10-instruction exit stubs, W^X-safe `patch_link`/`unpatch_link`, dynamic `link_entry_offset` |
| x86-64 backend | `codegen_backend_x86-64.c`, `codegen_backend_x86-64_uops.c` | Equivalent exit stubs and patch/unpatch for x86-64 |
| Dispatcher integration | `386_dynarec.c` | Calls `try_link_exit()` after block execution when `CODEBLOCK_NEEDS_LINKING` set, `_chain_remaining` counter for chain depth |

### Design

- **10-instruction cycle-guarded exit stub** (ARM64, includes chain_remaining check):
  ```
  LDR  W16, [CPUSTATE, #_cycles]
  CMP  W16, #0
  B.LE +24          ; bail if cycles <= 0
  LDR  W16, [CPUSTATE, #_chain_remaining]
  SUB  W16, W16, #1
  STR  W16, [CPUSTATE, #_chain_remaining]
  CMP  W16, #0
  B.GT +8           ; skip bail if chain_remaining > 0
  B    codegen_exit_rout  ; bail — exhausted
  B    target_or_exit     ; <-- patchable instruction
  ```
- **W^X-safe patching**: `pthread_jit_write_protect_np(0)` before write, `(1)` + `__clear_cache` after
- **FPU guard**: Refuse to link blocks with mismatched `CODEBLOCK_HAS_FPU` flag
- **Invalidation-safe**: `try_link_exit()` rejects blocks with `CODEBLOCK_IN_DIRTY_LIST` or `head_mem_block == NULL`
- **link_entry_offset = dynamically computed (skips full prologue)**: Linked blocks skip prologue (STPs + MOVs for 48-bit cpu_state address)
- **Lazy linking**: Only attempt once per block (`CODEBLOCK_NEEDS_LINKING` one-shot flag)
- **JMP exits only**: Only unconditional JMP exits have linkable stubs; conditional branches and epilogue exits are not linked
- **Pre-allocation**: `codegen_alloc(block, 40)` before emitting stub prevents buffer split across continuation blocks
- **Continuation detection**: Skip linkable exit when `block_write_data != block->data` (stub in continuation block)

### Commits (Phase 4 — both attempts)

#### First attempt (REVERTED)
| Commit | Description |
|--------|-------------|
| 387a88199 | Block linking core infrastructure |
| 5bdd7ae58 | link_entry_offset and link_epilogue_offset |
| 253573217 | Exit PC recording and dispatcher lazy linking |
| 8adc9d05b | x86-64 patchable exit stubs |
| 3ccff9e92 | Block linking research document |
| 2c541c36d | x86-64 patchable branch stubs |
| ae14fb750 | Fix 3 bugs from audit |
| 5919b02b5 | STATUS update |
| 4d5fbbb42 | **REVERT** — crashes on boot (3 bugs: W^X, cycle guard, B.cond encoding) |

#### Second attempt (FINAL — WORKING)
| Commit | Description |
|--------|-------------|
| da0a67d9e | Phase 4 retry plan with ARM64 audit |
| ffb449cb6 | Step 1: Core infrastructure — codeblock_t fields, link_init, try_link_exit, unlink |
| 3761522ef | Step 2: ARM64 backend — cycle-guarded exit stubs, W^X-safe patch/unpatch, link_entry_offset |
| e9cf557d8 | Steps 3-5: IR exit tracking, x86-64 backend, dispatcher integration |
| 3b2357d17 | Fix: one-shot CODEBLOCK_NEEDS_LINKING flag (was 2.1M spam/sec) |
| 0fab70e9a | Debug: stuck-address trap and MAX_LINKS binary search limit |
| 1933d7c57 | Debug: re-enable linking with enhanced diagnostics |
| e8c61f108 | **FINAL**: All fixes + invalidation guard + diagnostics removed |

### Bugs Found and Fixed

| Bug | Root Cause | Fix | Commit |
|-----|-----------|-----|--------|
| W^X violation (SIGBUS) | Patching JIT code without disabling write protection | `pthread_jit_write_protect_np()` wrapper | 3761522ef |
| No cycle guard (infinite chain hang) | Linked blocks never return to dispatcher | 5-instruction cycle-guarded stub | 3761522ef |
| B.cond encoding corruption | `branch_set_offset` uses OFFSET26, B.cond uses OFFSET19 | B.GT +8 (fixed short) + unconditional B | 3761522ef |
| Linking spam (2.1M/sec) | `try_link_exit` called on EVERY `code()` return | `CODEBLOCK_NEEDS_LINKING` one-shot flag | 3b2357d17 |
| JMP stub buffer overflow | Raw writes bypassed `codegen_alloc`, split across mem_blocks | `codegen_alloc(block, 40)` pre-allocation | e8c61f108 |
| Epilogue stub crash | Extra bytes caused excessive continuations | Removed entirely — JMP exits only | e8c61f108 |
| Continuation patch overflow | `patch_offset > BLOCK_MAX` in continuation block | Skip linkable exit when `block_write_data != block->data` | e8c61f108 |
| exit_pc missing CS base | `codegen_ir.c` stored raw EIP, not cs+eip | `+ cs` added to `_pending_exit_pc` | e8c61f108 |
| **Link to invalidated block** | `try_link_exit()` didn't check block validity | Reject `CODEBLOCK_IN_DIRTY_LIST` or `head_mem_block == NULL` | e8c61f108 |

### Key Lessons Learned

1. **W^X on macOS ARM64**: Any write to JIT code pages outside the recompilation path needs `pthread_jit_write_protect_np(0/1)` + `__clear_cache` guards.
2. **Cycle guards are mandatory**: Without them, linked block chains loop forever without returning to the dispatcher's cycle check.
3. **ARM64 branch encoding**: `B.cond` has +/- 1MB range (19-bit offset), unconditional `B` has +/- 128MB (26-bit). `host_arm64_branch_set_offset` is ONLY safe for unconditional B.
4. **Block invalidation frees code buffers**: `invalidate_block()` calls `codegen_allocator_free()` but does NOT remove the block from `codeblock_tree`. Link formation must explicitly check for invalidated blocks.
5. **Diagnostic-driven debugging**: Binary search on `chain_remaining` counter isolated the exact corrupting link out of millions. Always add logging first, observe, then fix.
6. **Agent hazard — lost fixes**: Agents can remove uncommitted fixes or re-introduce deleted code. Always verify critical fixes after every agent run.

## Next Phase

Phase 4 block linking is COMPLETE. Next recommended phase from the roadmap: **Phase 2 (Instruction Coverage — Core Gaps)**.
