# Status

## Current Phase: 1 — Bug Fixes & Quick Wins (COMPLETE)

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

## Next Phase

Phase 1 is COMPLETE. Next recommended phases (from PHASES.md):
- **Phase 3**: Dead flag elimination — Kildall's backward liveness analysis (10-30% gain)
- **Phase 4**: Block linking — direct jump patching between blocks (15-30% gain)
- **Phase 2**: More instruction coverage — IMUL, DIV, CMOVcc, BT/BS*, RCL/RCR, SHLD/SHRD
