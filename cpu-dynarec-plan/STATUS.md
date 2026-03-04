# Status

## Current Phase: 1 — Bug Fixes & Quick Wins (IN PROGRESS)

## Phase 1 Progress

### Bug Fixes (4/4 DONE)

| ID | File | Status | Commit | Description |
|----|------|--------|--------|-------------|
| BUG-1 | `codegen_ir_defs.h:533` | FIXED | f67fab199 | `is_a16` double-clear — removed spurious `uop->is_a16 = 0` |
| BUG-2 | `codegen.c:614` | FIXED | f67fab199 | `jump_cycles` return value — now captured |
| BUG-3 | `codegen_backend_arm64_uops.c:1879` | FIXED | 3684c49d7 | PFRSQRT — FMOV_S_ONE now writes to dest_reg |
| BUG-4 | `codegen_backend_arm64.c:344` | FIXED | 3684c49d7 | V8-V15 callee-saved — STP/LDP added in prologue/epilogue |

### ARM64 Backend Quick Fixes (1/2 DONE)

| Fix | Status | Commit | Notes |
|-----|--------|--------|-------|
| Narrow I-cache invalidation | DONE | 3684c49d7 | `codegen_allocator_clean_blocks` now takes `last_block_size` |
| W^X compliance (`pthread_jit_write_protect_np`) | NOT STARTED | — | Deferred, not blocking |

### Quick-Win Instruction Handlers (0/7 DONE)

| Instruction | Status | Notes |
|-------------|--------|-------|
| LAHF (9F) | NOT STARTED | Must use CALL_FUNC helper (no IREG_flags_B) |
| SAHF (9E) | NOT STARTED | Must use CALL_FUNC helper |
| BSWAP (0F C8-CF) | NOT STARTED | Shift/OR approach works, needs dispatch wiring |
| EMMS (0F 77) | NOT STARTED | CALL_FUNC helper safest |
| FPU constants (D9 E9-ED) | NOT STARTED | CALL_FUNC helpers for transcendentals (FLD1/FLDZ already exist) |
| FLDCW (D9 /5) | NOT STARTED | Needs CALL_FUNC, updates rounding mode |
| SETcc (0F 90-9F) | NOT STARTED | 16 ops, needs design work |

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

1. `f67fab199` — Fix is_a16 double-clear and jump_cycles dead code (BUG-1 + BUG-2)
2. `3684c49d7` — Fix ARM64 dynarec bugs and add backend improvements (BUG-3 + BUG-4 + I-cache)
