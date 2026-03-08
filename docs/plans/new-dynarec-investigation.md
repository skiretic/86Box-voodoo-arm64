# 2026-03-07 New Dynarec Investigation

> Historical baseline note (updated 2026-03-08): this document is the original investigation snapshot from 2026-03-07. Later branch work has already closed the page-0 sentinel bug, Phase 1 invalidation/reclamation hardening, arm64 `PMADDWD` parity, direct 3DNow table enable, the remaining direct 3DNow generator gap, the first measured non-REP `STOS`/`LODS` batch, the first two far-control/frame legality-first slices (`ENTER` / `0xc8`, `POPF` / `0x9d`), and the next non-protected measured batch (`MOVS` / `0xa5`, `IMUL r, r/m, imm8` / `0x6b`). For current status, use [new-dynarec-executive-summary.md](./new-dynarec-executive-summary.md), [new-dynarec-changelog.md](./new-dynarec-changelog.md), and [new-dynarec-optimization-overview.md](./new-dynarec-optimization-overview.md).

## Scope and method

This investigation is planning-only. No source changes were made.

Inspected areas:

- `src/codegen_new`
- `src/cpu/386_dynarec.c`
- `src/cpu/codegen_public.h`
- `src/include/86box/mem.h`
- `src/mem/mem.c`
- build wiring in `CMakeLists.txt`, `src/CMakeLists.txt`, `src/codegen_new/CMakeLists.txt`, and `src/cpu/CMakeLists.txt`
- related CPU table wiring in `src/cpu/cpu.c`
- related scripts/docs for tooling and testing

Quantification notes:

- Opcode coverage numbers below were derived by counting non-`NULL` entries in the `recomp_opcodes*` tables in `src/codegen_new/codegen_ops.c`.
- These counts measure direct recompilation coverage, not total guest ISA support. A `NULL` table entry or a generator that returns `0` still falls back to helper-call or interpreter execution.
- Backend parity for `uop_handlers` was checked by comparing the designated entries in `src/codegen_new/codegen_backend_arm64_uops.c` and `src/codegen_new/codegen_backend_x86-64_uops.c`.

## Executive summary

The new dynarec is a hybrid CPU execution pipeline: it marks blocks on first encounter, recompiles them on a later encounter, and then executes host code if the block remains valid. The main control flow lives in `src/cpu/386_dynarec.c:395-744`, the front-end decode/recompile path in `src/codegen_new/codegen.c:383-760`, and the block allocator/invalidation logic in `src/codegen_new/codegen_block.c:75-790`.

The highest-value problems are not missing backend uOP handlers. The backend uOP tables are structurally in parity, but the system has more serious issues in invalidation/reclamation, unsupported direct-recompile coverage, and missing observability. The most severe concrete risk is that the purgeable-page evict list uses real page 0 as its sentinel, which makes page 0 unrepresentable in the list and corrupts page-0 list metadata (`src/include/86box/mem.h:206-234`, `src/mem/mem.c:1855-1863`).

Coverage is materially incomplete. REP-prefixed instructions are never directly recompiled (`src/codegen_new/codegen.c:563-576`), softfloat mode drops `0F` direct coverage to 56/512 entries and disables direct recompilation for all `D8-DF` FPU escape tables (`src/codegen_new/codegen.c:411-417`, `src/codegen_new/codegen.c:452-557`, `src/cpu/cpu.c:607-645`, `src/config.c:489-492`), and at investigation time arm64 still lacked direct 3DNow coverage and missed at least `PMADDWD` that x86-64 supported (`src/codegen_new/codegen_ops.c:100-104`, `src/codegen_new/codegen_ops.c:126-130`, `src/codegen_new/codegen_ops.c:182-207`).

The current codebase also lacks the tooling needed to close these gaps safely. CPU dynarec logging is compile-time-only (`src/cpu/386_dynarec.c:60-75`), there are no repo-visible runtime counters or verify mode hooks for the CPU dynarec, and the repo contains Voodoo JIT verification/analyzer tooling that has no CPU dynarec equivalent (`scripts/README-jit-analyzer.md:1-104`, `scripts/analyze-jit-log.c`, `scripts/analyze-jit-log.py`, `scripts/test-with-vm.sh:1-18`).

## Current architecture overview

### Build and selection

- `DYNAREC` is enabled by default, and `NEW_DYNAREC` is forced `ON` on arm64 but optional elsewhere (`CMakeLists.txt:128-146`).
- `src/CMakeLists.txt:66-76` defines `USE_NEW_DYNAREC` and `USE_DYNAREC`; `src/CMakeLists.txt:192-208` selects `codegen_new` instead of the legacy `codegen` directory when `NEW_DYNAREC` is enabled.
- `src/codegen_new/CMakeLists.txt:18-68` builds a shared dynarec front-end plus either the x86-64 or arm64 backend.
- `src/cpu/CMakeLists.txt:18-60` always compiles `386_dynarec.c` and adds dynarec opcode/timing sources when `DYNAREC` is enabled.

### CPU integration

- `src/cpu/cpu.c:590-645` wires the dynarec opcode tables and selects softfloat vs non-softfloat FPU helper tables.
- `src/cpu/cpu.c:1613-1629` switches to K6/K6-2 `0F` tables and the enhanced 3DNow table for `CPU_K6_2P` and `CPU_K6_3P`.
- `src/cpu/cpu.c:1852-1856` routes 386-class CPU execution to `exec386_dynarec` when `cpu_use_dynarec` is enabled.
- `src/cpu/386_dynarec.c:782-787` still falls back to full interpretation when `cpu_force_interpreter`, `cpu_override_dynarec`, or `!CACHE_ON()` is true.

### Block lifecycle

- First encounter is mark-only, not recompilation (`src/cpu/386_dynarec.c:644-735`).
- Recompilation of an existing valid block happens on the next eligible encounter (`src/cpu/386_dynarec.c:537-643`).
- A fully recompiled block executes directly through `block->data[BLOCK_START]` (`src/cpu/386_dynarec.c:515-536`).
- Repeated invalidation degrades a block from normal page-mask tracking to `CODEBLOCK_BYTE_MASK`, then to `CODEBLOCK_NO_IMMEDIATES` (`src/cpu/386_dynarec.c:491-498`).

### Front-end decode and fallback model

- `src/codegen_new/codegen.c:409-580` parses prefixes and selects the interpreter table plus the direct-recompile table.
- If a direct recompiler handler exists and returns nonzero, the instruction stays on the direct path (`src/codegen_new/codegen.c:682-696`).
- Otherwise, the block emits a helper-call uOP through `uop_CALL_INSTRUCTION_FUNC` (`src/codegen_new/codegen.c:699-748`).
- This means there are three practical execution modes:
  - full block interpretation
  - compiled block with helper-call fallbacks
  - fully direct-recompiled instructions inside a compiled block

### Memory / invalidation integration

- The new dynarec exposes 64-byte page masks through `src/cpu/codegen_public.h:42-57`.
- `src/include/86box/mem.h:198-236` extends `page_t` with:
  - coarse `dirty_mask` / `code_present_mask`
  - byte-level `byte_dirty_mask` / `byte_code_present_mask`
  - an evict-list link pair
- `src/mem/mem.c:1880-1969` marks coarse and byte-level dirty state on RAM writes and may add the page to the purgeable-page list.
- `src/codegen_new/codegen_block.c:454-502` invalidates blocks by intersecting per-block masks with page dirty masks.

## Findings and gaps

### Correctness and stability risks

#### 1. Critical: the purgeable-page list uses real page 0 as its sentinel

Evidence:

- `EVICT_NOT_IN_LIST` is `0xffffffff`, but the list head sentinel is `0` and `page_in_evict_list()` treats any `evict_prev != EVICT_NOT_IN_LIST` as "in list" (`src/include/86box/mem.h:206-234`).
- `page_add_to_evict_list()` writes through `pages[purgable_page_list_head]`, including when the list is empty (`purgable_page_list_head == 0`) (`src/mem/mem.c:1855-1863`).

Impact:

- Page 0 cannot be represented cleanly as a list member.
- Adding any page to an empty list mutates `pages[0].evict_prev`.
- `page_in_evict_list(&pages[0])` can become true even when page 0 is not actually linked.

This is a concrete correctness bug, not speculation.

#### 2. High: byte-mask dirty pages appear reclaimable in theory but not in the allocator purge path

Evidence:

- Byte-level code overlap adds a page to the evict list (`src/mem/mem.c:1897-1901`, `src/mem/mem.c:1926-1936`, `src/mem/mem.c:1960-1967`).
- The reclaim path only checks the list head, and only for coarse `page->code_present_mask & page->dirty_mask` overlap (`src/codegen_new/codegen_block.c:163-176`).
- `codegen_check_flush()` leaves `remove_from_evict_list` false when byte-mask overlap is seen (`src/codegen_new/codegen_block.c:494-501`).

Inference:

Byte-mask-only dirty pages can remain at the head of the purgeable-page list without being reclaimed by `codegen_purge_purgable_list()`. Under block pressure, this can force `block_free_list_get()` into random block deletion instead of reclaiming the page that is already known dirty (`src/codegen_new/codegen_block.c:184-208`, `src/codegen_new/codegen_block.c:436-450`).

This needs confirmation with runtime counters, but the control flow strongly points in that direction.

#### 3. Medium: known direct-recompile correctness regressions are already called out in the tree

Evidence:

- `src/codegen_new/codegen_ops.c:261` documents a direct recompilation bug for `D9 44` that breaks Blood II gameplay music.
- `src/codegen_new/codegen_ops_mov.c:300-304` intentionally returns `0` for one `MOV imm8 -> mem` case because recompiling it breaks NT 3.x NTVDM.

Impact:

- These are explicit, known correctness holes in the direct path.
- They also imply that current coverage counts overstate "safe direct coverage" if some direct handlers still require targeted exclusions.

#### 4. Medium: self-modifying-code handling degrades blocks instead of fixing root causes

Evidence:

- Repeated invalidation upgrades a block to `CODEBLOCK_BYTE_MASK` and then `CODEBLOCK_NO_IMMEDIATES` (`src/cpu/386_dynarec.c:491-498`).
- `CODEBLOCK_BYTE_MASK` blocks are capped at roughly 103 source bytes depending on alignment instead of 1000 bytes (`src/cpu/386_dynarec.c:540`, `src/cpu/386_dynarec.c:648`).

Impact:

- This is a pragmatic safety valve, but it is also a sign that SMC-heavy paths are expected to degrade materially.
- It likely avoids some correctness problems at the cost of major performance cliffs.

### Missing functionality and unsupported direct paths

#### Quantified direct-recompile coverage

Derived from the `recomp_opcodes*` tables in `src/codegen_new/codegen_ops.c`.

| Table | arm64 | x86-64 | Notes |
|---|---:|---:|---|
| `recomp_opcodes` | 380 / 512 | 380 / 512 | 132 `NULL` entries on both backends |
| `recomp_opcodes_0f` | 140 / 512 | 142 / 512 | x86-64 has two extra direct entries |
| `recomp_opcodes_0f_no_mmx` | 56 / 512 | 56 / 512 | softfloat cliff |
| `recomp_opcodes_3DNOW` | 0 / 256 | 16 / 256 | arm64 table compiled out |
| `recomp_opcodes_d8` | 512 / 512 | 512 / 512 | only when not softfloat |
| `recomp_opcodes_d9` | 250 / 512 | 250 / 512 | known `D9 44` issue remains |
| `recomp_opcodes_da` | 386 / 512 | 386 / 512 | partial |
| `recomp_opcodes_db` | 144 / 512 | 144 / 512 | sparse |
| `recomp_opcodes_dc` | 512 / 512 | 512 / 512 | full table coverage |
| `recomp_opcodes_dd` | 272 / 512 | 272 / 512 | partial |
| `recomp_opcodes_de` | 482 / 512 | 482 / 512 | near-complete |
| `recomp_opcodes_df` | 242 / 512 | 242 / 512 | partial |

Important interpretation:

- Base integer coverage is incomplete but substantial.
- Extended opcode coverage is still sparse.
- Softfloat mode is a major direct-coverage cliff.
- At investigation time, arm64 lacked all direct 3DNow support and missed some MMX coverage present on x86-64. That specific parity gap has since been closed on this branch.

#### REP-prefixed instructions are helper-call only

Evidence:

- `src/codegen_new/codegen.c:563-576` explicitly sets `recomp_op_table = NULL` for both `REPNE` and `REPE`.

Impact:

- String-heavy workloads do not get direct recompilation for REP paths.
- These instructions still execute via helper-call inside compiled blocks, but they never take the direct recompiler fast path.

#### Softfloat disables large parts of direct recompilation

Evidence:

- `0F` uses `recomp_opcodes_0f_no_mmx` when `fpu_softfloat` is enabled (`src/codegen_new/codegen.c:411-417`).
- `D8-DF` set `recomp_op_table = NULL` when `fpu_softfloat` is enabled (`src/codegen_new/codegen.c:452-557`).
- `fpu_softfloat` is configuration-driven and can be forced by machine flags (`src/config.c:489-492`).

Impact:

- In softfloat mode, `0F` direct coverage drops to 56/512 entries.
- All direct FPU escape recompilation disappears even though the dynarec still emits helper-call paths.
- Softfloat-only machines are likely under-optimized and under-tested.

#### Explicit helper-call bailout sites remain common

Evidence:

- `codegen_generate_call()` treats a direct handler returning `0` as a helper-call fallback (`src/codegen_new/codegen.c:682-748`).
- There are 86 explicit `return 0;` bailout sites across `src/codegen_new/codegen_ops_*.c`.

Highest concentrations:

- `src/codegen_new/codegen_ops_shift.c`: 26
- `src/codegen_new/codegen_ops_misc.c`: 18
- `src/codegen_new/codegen_ops_branch.c`: 14
- `src/codegen_new/codegen_ops_arith.c`: 10
- `src/codegen_new/codegen_ops_mov.c`: 4
- `src/codegen_new/codegen_ops_jump.c`: 4

Interpretation:

- Table coverage alone understates fallback frequency.
- Any benchmark or validation plan should track both `NULL` table entries and "implemented but bailed out" cases.

### Backend asymmetries

#### 1. Direct opcode coverage differs more in front-end tables than in uOP handler tables

Evidence:

- arm64 and x86-64 both define the `uop_handlers` array (`src/codegen_new/codegen_backend_arm64_uops.c:2915`, `src/codegen_new/codegen_backend_x86-64_uops.c:2871`).
- Measured parity: both backends expose 143 designated `uop_handlers` entries with no index-coverage mismatch.

Interpretation:

- Backend asymmetry is not primarily "missing uOP handler X on arm64".
- The bigger asymmetries are:
  - missing direct opcode entries
  - backend-specific optimizations
  - backend-specific code quality

#### 2. Investigation-time arm64 3DNow / `PMADDWD` parity gap

Evidence:

- `recomp_opcodes_3DNOW` is compiled out entirely on arm64 (`src/codegen_new/codegen_ops.c:182-207`).
- `PMADDWD` is present in x86-64 `0F` tables but compiled out on arm64 (`src/codegen_new/codegen_ops.c:100-104`, `src/codegen_new/codegen_ops.c:126-130`).
- `src/cpu/cpu.c:1625-1629` still routes K6-2+/K6-3+ CPUs to enhanced 3DNow dynarec opcode tables on the CPU side.

Impact at the time:

- Backend feature parity was materially worse for AMD/K6-era MMX/3DNow workloads on arm64.
- Some guest CPU models advertised front-end coverage that the arm64 direct recompiler did not actually match.

Follow-up status on this branch:

- This parity gap is now closed; the branch has direct arm64 `PMADDWD` parity, direct 3DNow table enable, and the remaining direct 3DNow generator batch in tree.

#### 3. x86-64 has a backend-only `MOV_IMM` optimization that arm64 lacks

Evidence:

- `src/codegen_new/codegen_ir.c:113-119` enables a special `MOV_IMM` store-direct optimization behind `CODEGEN_BACKEND_HAS_MOV_IMM`.
- `src/codegen_new/codegen_backend_x86-64.h:14` defines that macro.
- `src/codegen_new/codegen_backend_arm64.h:1-30` does not.

Impact:

- x86-64 can avoid some needless register materialization that arm64 currently cannot, even though arm64 has the larger integer register budget.

#### 4. Host register budgets differ sharply

Evidence:

- arm64 exposes 10 integer and 8 FP host registers (`src/codegen_new/codegen_backend_arm64_defs.h:114-115`, `src/codegen_new/codegen_backend_arm64.c:50-72`).
- x86-64 exposes 3 integer and 7 FP host registers (`src/codegen_new/codegen_backend_x86-64_defs.h:49-50`, `src/codegen_new/codegen_backend_x86-64.c:46-70`).

Interpretation:

- arm64 should have room for more aggressive scheduling and fewer spills.
- If arm64 still underperforms, the likely causes are missing peepholes, helper-call frequency, or eviction/warmup behavior rather than raw register scarcity.

### Architectural debt

#### 1. The executable allocator is fixed-size and falls back to random block deletion

Evidence:

- The allocator reserves `131072 * 0x3c0` bytes of executable memory, about 120 MiB (`src/codegen_new/codegen_allocator.h:17-21`).
- On mem-block exhaustion, it walks all code blocks deleting others until space appears or aborts (`src/codegen_new/codegen_allocator.c:112-128`).
- When no free blocks are available, block reuse can degrade into `codegen_delete_random_block()` (`src/codegen_new/codegen_block.c:184-208`, `src/codegen_new/codegen_block.c:436-450`).

Impact:

- This is simple but not workload-aware.
- Random eviction makes performance noisy and makes debugging hot/cold behavior harder.

#### 2. Warmup is intentionally conservative

Evidence:

- New blocks are marked but not compiled on first encounter (`src/cpu/386_dynarec.c:644-735`).

Impact:

- Every hot block pays at least one fully interpreted pass before compilation.
- That is a safe default, but it inflates warmup cost and can hide direct-coverage benefits on short-lived workloads.

#### 3. Byte-mask blocks disable loop unrolling

Evidence:

- `codegen_can_unroll()` immediately returns false for `CODEBLOCK_BYTE_MASK` blocks (`src/codegen_new/codegen_ops_helpers.h:115-126`).
- Full unrolling is otherwise capped by `UNROLL_MAX_UOPS = 1000` and `UNROLL_MAX_COUNT = 10` (`src/codegen_new/codegen_ops_helpers.c:38-40`, `src/codegen_new/codegen_ops_helpers.c:66-75`).

Impact:

- The blocks that are already most expensive because of SMC pressure also lose loop-unrolling wins.

#### 4. The public dynarec flush hook is a stub

Evidence:

- `flushmmucache()` and `flushmmucache_pc()` call `codegen_flush()` (`src/mem/mem.c:193-214`, `src/mem/mem.c:231-240`).
- `codegen_flush()` currently does nothing (`src/codegen_new/codegen_block.c:786-790`).

Inference:

- Current correctness depends on page dirty tracking and status matching being sufficient for all invalidation cases.
- That may be fine today, but the public API shape implies an invalidation capability that is not actually implemented.

#### 5. Memory allocation still carries an explicit dynarec workaround

Evidence:

- RAM allocation adds 16 extra bytes "to mitigate some dynarec recompiler memory access quirks" (`src/mem/mem.c:2771-2778`).

Impact:

- This is a concrete sign that the memory access assumptions are still not fully cleanly modeled.
- Even if harmless, it should be treated as debt until the underlying quirk is documented or removed.

### Performance opportunities

#### 1. Fix purge / eviction behavior before chasing micro-optimizations

The current reclaim path likely leaves reclaimable work on the table and then deletes random blocks. That has larger upside than tuning individual arithmetic sequences because it directly affects cache residency and recompilation churn (`src/codegen_new/codegen_block.c:163-208`, `src/codegen_new/codegen_block.c:436-450`).

#### 2. Bring arm64 optimization level closer to x86-64

- Port or replace the x86-64-only `MOV_IMM` optimization (`src/codegen_new/codegen_ir.c:113-119`, `src/codegen_new/codegen_backend_x86-64.h:14`).
- Exploit the wider arm64 register file more aggressively (`src/codegen_new/codegen_backend_arm64_defs.h:114-115`).

#### 3. Improve reciprocal / rsqrt implementations

Both backends have explicit TODOs here:

- arm64: `src/codegen_new/codegen_backend_arm64_uops.c:1858-1862`, `src/codegen_new/codegen_backend_arm64_uops.c:1876-1881`
- x86-64: `src/codegen_new/codegen_backend_x86-64_uops.c:1935-1941`, `src/codegen_new/codegen_backend_x86-64_uops.c:1957-1963`

These are good targeted wins once the correctness and observability basics are in place.

#### 4. Revisit SMC degradation heuristics with data

Current behavior progressively shrinks block size, disables unrolling, and eventually avoids immediate inlining (`src/cpu/386_dynarec.c:491-498`, `src/cpu/386_dynarec.c:540`, `src/cpu/386_dynarec.c:648`). That is probably correct as a defensive fallback, but the policy is heuristic-heavy and should be driven by measured invalidation/fallback data rather than intuition.

## Observability and testing gaps

### Missing observability

- CPU dynarec logging is compile-time-only (`src/cpu/386_dynarec.c:60-75`).
- There is no repo-visible CPU dynarec counter set for:
  - block compilation count
  - helper-call fallbacks
  - `NULL` opcode-table hits
  - bailout-by-opcode counts
  - invalidation / dirty-list / byte-mask transitions
  - allocator pressure / random eviction
- The only obvious user-facing control is "Force interpretation" (`src/86box.c:318-322`).

### Missing validation tooling

- The Voodoo JIT has a documented debug/verify pipeline and a standalone analyzer (`scripts/README-jit-analyzer.md:1-104`).
- The CPU dynarec has no comparable verify mode, no log analyzer, and no benchmark harness in the repo.
- The only obvious runtime helper script found was `scripts/test-with-vm.sh:1-18`, which is a generic VM launcher.
- The repo does contain a general-purpose unittester device (`doc/specifications/86box-unit-tester.md:1-49`, `src/device/unittester.c:1-160`), but it is not currently wired into CPU dynarec validation.

## Risk ranking

| Rank | Category | Issue | Why it matters |
|---|---|---|---|
| 1 | Correctness / stability | Evict-list sentinel collides with page 0 | Concrete metadata corruption and inability to represent page 0 correctly |
| 2 | Correctness / performance | Byte-mask dirty pages are likely not reclaimed through the purgeable list | Can leave stale dirty pages queued and force random block eviction |
| 3 | Coverage / performance | REP paths, softfloat paths, and investigation-time arm64 3DNow/MMX gaps | Large direct-recompile blind spots on real workloads |
| 4 | Correctness | Known direct-recompile regressions remain in tree | Existing guest-visible bugs already acknowledged in source |
| 5 | Observability / testing | No CPU verify mode, counters, or analyzer | Makes every future coverage or perf change riskier and slower to validate |
| 6 | Architectural debt | Fixed executable pool and random eviction | Likely churn and noisy performance under pressure |

## Recommended multiphase plan

### Phase 0: observability and reproducibility first

Goals:

- Make the dynarec measurable before changing behavior.

Work:

- Add per-block and per-opcode counters for:
  - direct recompiles
  - helper-call fallbacks
  - `NULL` table hits
  - generator-returned-0 bailouts
  - dirty-list transitions
  - byte-mask / no-immediates escalation
  - allocator pressure and random evictions
- Add a low-overhead structured trace mode for block lifecycle events.
- Add a CPU dynarec A/B validation mode that can compare selected blocks or selected opcodes against forced interpretation.

Exit criteria:

- A single run can answer "what compiled, what fell back, what invalidated, and why?"

### Phase 1: invalidation and reclamation hardening

Goals:

- Fix correctness issues in cache invalidation and reclaim behavior.

Work:

- Fix the page-0 evict-list sentinel problem.
- Make purgeable-page reclamation work for byte-mask-only pages, not just coarse mask overlap.
- Ensure the purge path can skip stale head entries instead of giving up after one page.
- Add targeted tests for:
  - page 0 code writes
  - page-boundary writes
  - byte-mask block invalidation
  - remapped-memory invalidation

Exit criteria:

- No list corruption, deterministic page reclamation, and no random eviction caused by stale purgeable-page state.

### Phase 2: coverage closure and explicit policy

Goals:

- Reduce silent fallback surface area.

Work:

- Build an explicit guest-visible support matrix for direct recompilation:
  - REP
  - softfloat
  - MMX
  - 3DNow
  - per-backend deltas
- Decide which gaps are bugs to fix vs intentional non-goals that should be traced and counted.
- Tackle the most valuable direct-coverage gaps first:
  - REP string paths
  - known `return 0` bailout clusters
  - at investigation time, arm64 `PMADDWD` / 3DNow parity or explicit disable policy

Exit criteria:

- Direct coverage gaps are intentional, measured, and documented rather than accidental.

### Phase 3: backend-specific performance work

Goals:

- Improve hot-path quality after correctness and visibility are under control.

Work:

- Port x86-64 `MOV_IMM` style optimization to arm64 or replace it with a better arm64-specific equivalent.
- Replace random eviction with a policy tied to age, hotness, or recent invalidation history.
- Optimize `PFRCP` / `PFRSQRT`.
- Revisit first-hit mark-only behavior and SMC degradation thresholds using collected counters.

Exit criteria:

- Performance work is driven by data instead of guesswork.

### Phase 4: release-quality validation and benchmarking

Goals:

- Turn dynarec work into a repeatable regression gate.

Work:

- Add a benchmark corpus and a correctness corpus.
- Run cross-backend and dynarec-vs-interpretation sweeps in CI or scripted nightly runs.
- Set thresholds for fallback rate, invalidation rate, and benchmark deltas.

Exit criteria:

- New dynarec changes are gated by both correctness and performance data.

## Suggested validation / benchmark strategy for later phases

### Correctness strategy

- Use forced interpretation (`src/86box.c:318-322`) as the reference path.
- Add block-level or opcode-level shadow execution for selected workloads, not whole-machine always-on verification at first.
- Prioritize workloads that stress the current weak spots:
  - self-modifying code
  - REP string instructions
  - page-boundary instruction fetch / writeback
  - softfloat-only machines
  - x87-heavy code
  - MMX / 3DNow code on K6-class guests
  - NT 3.x NTVDM
  - Blood II

### Benchmark strategy

- Record at minimum:
  - blocks marked
  - blocks recompiled
  - helper-call fallbacks
  - `NULL` table hits
  - bailout sites hit
  - invalidations
  - byte-mask / no-immediates transitions
  - allocator usage
  - random evictions
- Compare:
  - dynarec vs forced interpretation
  - arm64 vs x86-64
  - softfloat on vs off where machine model permits
- Use both short-running and long-running workloads so warmup cost and steady-state cost are both visible.

### Candidate workload set

- DOS CPU / memory tests with SMC behavior
- Windows 9x boot and application launch
- Windows NT 3.x / NT 4 boot and NTVDM execution
- K6/K6-2/K6-3 MMX and 3DNow microbenchmarks
- x87-heavy synthetic tests and real application traces
- remapped-memory / chipset-specific memory-layout tests

## Top 5 highest-value next steps

1. Fix the purgeable-page list design so page 0 is not the sentinel and byte-mask dirty pages can actually be reclaimed.
2. Add CPU dynarec observability: counters, structured block lifecycle traces, and fallback accounting.
3. Build a focused correctness matrix around the known weak spots: REP, softfloat, SMC, page boundaries, NT 3.x NTVDM, and Blood II.
4. Produce and maintain a direct-recompile support matrix by backend so arm64/x86-64 deltas are explicit instead of discovered ad hoc.
5. Replace random block eviction and heuristic-only warmup/SMC degradation with policies justified by measured invalidation and hotness data.
