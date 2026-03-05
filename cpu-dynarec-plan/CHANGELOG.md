# CPU Dynarec Changelog

## Phase 4 — Block Linking (COMPLETE)

### 2026-03-05: Phase 4 finalized — block linking working

**Final commit:** `e8c61f108` — Phase 4: lazy block linking with cycle-guarded exit stubs

All diagnostics stripped, code cleaned up, Win98 boots to desktop on ARM64.

### 2026-03-05: Root cause found — linking to invalidated blocks

Diagnostic logging revealed the smoking gun: `try_link_exit()` was forming links
to blocks that had already been invalidated by `invalidate_block()`. The invalidated
block's code buffer was freed by `codegen_allocator_free()`, but the block remained
in `codeblock_tree` (searchable by physical address). When the stale link was followed,
execution jumped into reclaimed/overwritten code, corrupting the PC.

**Timeline from diagnostics:**
```
COMPILED blk#16291  pc=262a4  data=0x1248177c0    <- block compiled
INVALIDATE blk#16291  pc=262a4                     <- code buffer freed
LINK-FORM src=16293 -> tgt=16291  data=0x1248177c0 <- LINK TO DEAD BLOCK
DISP[1210293-1210298]: block 16293 runs 6x (cycle guard bailing)
DISP[1210299]: block 16265 at pc=202da (HALT loop) <- stale link followed
```

**Fix:** Two guards in `try_link_exit()`:
1. Reject blocks with `CODEBLOCK_IN_DIRTY_LIST` flag (set during invalidation)
2. Reject blocks with `head_mem_block == NULL` (code buffer freed)

### 2026-03-04: Binary search isolates corrupting link

Used `chain_remaining` counter to binary search across ~1.2M dispatches.
Narrowed to: link from blk_pc=0x2627A to blk#16291 (pc=0x262A4), patched
at dispatch ~1210293, followed at dispatch 1210299. Target block sets pc=0x0013
(wrong), leading to stuck halt loop at pc=0x202DA.

### 2026-03-04: Debugging session — 9 bugs found across two attempts

**First attempt** (8 commits, then reverted in 4d5fbbb42):
- W^X violation (SIGBUS) — patching without write protection
- No cycle guard — infinite linked chains
- B.cond encoding corruption — wrong offset field size

**Second attempt** (commits ffb449cb6..e8c61f108):
- Linking spam (2.1M/sec) — missing one-shot flag
- JMP stub buffer overflow — missing pre-allocation
- Epilogue stub crash — removed entirely
- Continuation patch overflow — continuation block detection
- exit_pc missing CS base — `+ cs` added
- Link to invalidated block — dirty list + mem_block check

---

## Phase 3 — Dead Flag Elimination (COMPLETE)

### 2026-03-03: Implementation complete
- Commits: `693af2e27`, `7b8609839`, `87b28a4fe`
- Backward liveness analysis in codegen_ir.c (`codegen_ir_eliminate_dead_flags()`)
- No backend changes needed — existing DCE cascade handles eliminated UOPs

---

## Phase 1 — Bug Fixes + Quick-Win Handlers (COMPLETE)

### Commits
- `f67fab199` — BUG-2 (jump_cycles) + BUG-1 (is_a16, later reverted)
- `3684c49d7` — BUG-3 (PFRSQRT) + BUG-4 (V8-V15) + I-cache narrowing
- `87c56036d` — 7 instruction handlers (LAHF/SAHF/BSWAP/EMMS/FPU constants/FLDCW/SETcc)
- `07fd7fd0a` — Revert is_a16 fix (backend bug)

---

## Phase 0 — Research & Audit (COMPLETE)

- 6 audit documents in `cpu-dynarec-plan/research/`
- Phase roadmap in `PHASES.md`
- Executive summary in `EXECUTIVE-SUMMARY.md`
